// Copyright 2022 The XLS Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>

#include "xls/passes/rematerialization_pass.h"

#include "absl/container/flat_hash_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"

#include "xls/common/logging/logging.h"
#include "xls/common/status/status_macros.h"
#include "xls/data_structures/submodular.h"
#include "xls/ir/node.h"
#include "xls/ir/node_iterator.h"
#include "xls/ir/op.h"
#include "xls/passes/cse_pass.h"
#include "xls/passes/dce_pass.h"

namespace xls {

namespace {

int64_t NumberOfPipelineStages(
    const absl::flat_hash_map<Node*, int64_t>& schedule) {
  int64_t max_stage = -1;
  for (const auto& [node, stage] : schedule) {
    max_stage = std::max(max_stage, stage);
  }
  return max_stage + 1;
}

// This data structure is similar to `ScheduleCycleMap`, except it answers the
// question "what nodes are in a given pipeline stage?".
using InverseSchedule = std::vector<absl::flat_hash_set<Node*>>;

// Compute an `InverseSchedule` from a given `ScheduleCycleMap`.
InverseSchedule InvertSchedule(
    const absl::flat_hash_map<Node*, int64_t>& schedule) {
  InverseSchedule result;
  result.resize(NumberOfPipelineStages(schedule));
  for (const auto& [node, stage] : schedule) {
    result[stage].insert(node);
  }
  return result;
}

using Delay = int64_t;

// This data structure records the total delay along the longest path 
// between each pair of node.
using LongestPathLength =
    absl::flat_hash_map<Node*, absl::flat_hash_map<Node*, Delay>>;

absl::StatusOr<LongestPathLength> LongestNodePaths(
    FunctionBase* f, const DelayEstimator& delay_estimator) {
  absl::flat_hash_map<Node*, Delay> delay_map;

  for (Node* node : TopoSort(f)) {
    XLS_ASSIGN_OR_RETURN(delay_map[node],
                         delay_estimator.GetOperationDelayInPs(node));
  }

  LongestPathLength result;

  for (Node* node : TopoSort(f)) {
    // The graph must be acyclic, so the longest path from any vertex to
    // itself has weight equal to the weight of that vertex.
    result[node] = {{node, delay_map.at(node)}};
  }

  for (Node* node : TopoSort(f)) {
    for (auto& [source, targets] : result) {
      for (Node* operand : node->operands()) {
        if (targets.contains(operand)) {
          Delay new_delay = targets[operand] + delay_map.at(node);
          targets[node] = targets.contains(node)
                              ? std::max(targets.at(node), new_delay)
                              : new_delay;
        }
      }
    }
  }

  return result;
}

struct Slack {
  // The delay of the longest path starting at an entry node of the current
  // pipeline stage and ending at a given node, not including the delay of that
  // node.
  Delay longest_path_into;

  // The delay of the longest path starting at a given node and ending at an
  // exit node of the current pipeline stage, not including the delay of the
  // given node.
  Delay longest_path_out_of;

  // The slack of the given node, which is equal to
  // `stage_critical_path - (longest_path_into + delay + longest_path_out_of)`
  // where `delay` is the delay of the given node.
  Delay slack;

  // The critical path delay of the stage of a given node.
  Delay stage_critical_path;
};

absl::flat_hash_map<Node*, Slack> ComputeSlack(
    FunctionBase* f, const InverseSchedule& inverse_schedule,
    const LongestPathLength& longest) {
  absl::flat_hash_map<Node*, Slack> slack;

  for (int64_t stage = 0; stage < inverse_schedule.size(); ++stage) {
    absl::flat_hash_set<Node*> stage_nodes = inverse_schedule[stage];
    // entry_nodes's requirement:
    //   (has no operand) OR (all operands are scheduled in previous stages)
    absl::flat_hash_set<Node*> entry_nodes;
    // exit_nodes's requirement:
    //   (has users) AND (all users are scheduled in following stages)
    absl::flat_hash_set<Node*> exit_nodes;

    for (Node* node : stage_nodes) {
      auto operands = node->operands();
      if (std::none_of(operands.begin(), operands.end(),
                       [&](Node* n) { return stage_nodes.contains(n); })) {
        entry_nodes.insert(node);
      }
      auto users = node->users();
      if (!users.empty()  // omit dead nodes
          && std::none_of(users.begin(), users.end(),
                          [&](Node* n) { return stage_nodes.contains(n); })) {
        exit_nodes.insert(node);
      }
    }

    // longest path between any pair of entry/exit
    int64_t stage_critical_length = 0;

    for (Node* entry : entry_nodes) {
      for (Node* exit : exit_nodes) {
        if (longest.at(entry).contains(exit)) {
          stage_critical_length =
              std::max(stage_critical_length, longest.at(entry).at(exit));
        }
      }
    }

    for (Node* node : stage_nodes) {
      bool path_exists = false;

      // longest path from any stage entry node to current node
      int64_t entry_to_node_max = 0;
      for (Node* entry : entry_nodes) {
        if (longest.at(entry).contains(node)) {
          entry_to_node_max =
              std::max(entry_to_node_max, longest.at(entry).at(node));
          path_exists = true;
        }
      }

      // longest path from current node to any stage exit node
      int64_t node_to_exit_max = 0;
      for (Node* exit : exit_nodes) {
        if (longest.at(node).contains(exit)) {
          node_to_exit_max =
              std::max(node_to_exit_max, longest.at(node).at(exit));
          path_exists = true;
        }
      }

      if (!path_exists) {
        // If a node is dead, there may not exist a path
        continue;
      }

      // Avoid double-counting the delay of the node itself
      Delay node_delay = longest.at(node).at(node);
      entry_to_node_max -= node_delay;
      node_to_exit_max -= node_delay;

      int64_t entry_to_exit_through_node_max =
          entry_to_node_max + node_delay + node_to_exit_max;

      slack[node] =
          Slack{entry_to_node_max, node_to_exit_max,
                stage_critical_length - entry_to_exit_through_node_max,
                stage_critical_length};
    }
  }

  return slack;
}

}  // namespace

// An opportunity for rematerialization.
// TODO(taktoa): somehow refactor this so that it talks about rematerializing
// an edge rather than a node.
// TODO(zihao): delete it after the refactor implementation is done
struct RematOpportunity {
  // The node that may be rematerialized.
  Node* to_rematerialize;
  // A dead node representing the rematerialization, which `to_rematerialize`
  // can be replaced with.
  Node* rematerialization;
  // The amount of area saved by applying this opportunity, not including the
  // cost incurred by any nodes added.
  double quality;

  friend bool operator==(const RematOpportunity& lhs,

                         const RematOpportunity& rhs) {
    return (lhs.to_rematerialize == rhs.to_rematerialize) &&
           (lhs.rematerialization == rhs.rematerialization);
  }

  friend bool operator<(const RematOpportunity& lhs,
                        const RematOpportunity& rhs) {
    std::tuple<Node*, Node*> lhs_tuple{lhs.to_rematerialize,
                                       lhs.rematerialization};
    std::tuple<Node*, Node*> rhs_tuple{rhs.to_rematerialize,
                                       rhs.rematerialization};
    return lhs_tuple < lhs_tuple;
  }

  template <typename H>
  friend H AbslHashValue(H h, const RematOpportunity& ro) {
    return H::combine(std::move(h), ro.to_rematerialize, ro.rematerialization,
                      ro.quality);
  }
};

// TODO(zihao): A proposed replacement for `RematOpportunity`. It talks about "edge".
struct RematEdgeOpportunity {
  // The crose-stage data dependence edge.
  // "cross-stage" means "stage[edge_src] < stage[edge_dst]".
  Node* edge_src;
  Node* edge_dst;
  // A dead node representing the rematerialization, which 
  // the USE of `edge_src` can be replaced with.
  Node* rematerialization;
  // The amount of area saved by applying this opportunity, not including the
  // cost incurred by any nodes added.
  double quality;

  friend bool operator==(const RematEdgeOpportunity& lhs,

                         const RematEdgeOpportunity& rhs) {
    return (lhs.edge_src == rhs.edge_src) &&
           (lhs.edge_dst == rhs.edge_dst) &&
           (lhs.rematerialization == rhs.rematerialization);
  }

  friend bool operator<(const RematEdgeOpportunity& lhs,
                        const RematEdgeOpportunity& rhs) {
    std::tuple<Node*, Node*, Node*> lhs_tuple{lhs.edge_src, lhs.edge_dst,
                                       lhs.rematerialization};
    std::tuple<Node*, Node*, Node*> rhs_tuple{rhs.edge_src, rhs.edge_dst,
                                       rhs.rematerialization};
    return lhs_tuple < lhs_tuple;
  }

  template <typename H>
  friend H AbslHashValue(H h, const RematEdgeOpportunity& ro) {
    return H::combine(std::move(h), ro.edge_src, ro.edge_dst, 
                      ro.rematerialization, ro.quality);
  }
};

// Returns for a given dead node, the largest "contiguous" set of dead nodes
// that feed into this node.
// A node is within this set iff all paths starting at that node and ending
// at the given node only touch dead nodes, and at least one such path exists.
// corner case: 
//   if "node" in "dead_nodes", the return set will contains "node"
//   in this case, returning set will never be empty
absl::flat_hash_set<Node*> ComputeDeadNodeChunk(
    const absl::flat_hash_set<Node*>& dead_nodes, Node* node) {
  std::vector<Node*> stack;
  stack.push_back(node);
  absl::flat_hash_set<Node*> discovered;
  while (!stack.empty()) {
    Node* popped = stack.back();
    stack.pop_back();
    if (dead_nodes.contains(popped) && !discovered.contains(popped)) {
      discovered.insert(popped);
      for (Node* child : popped->operands()) {
        stack.push_back(child);
      }
    }
  }
  return discovered;
}

// Compute the reduced pipeline stage number by doing remat on edge "src -> dst".
// The result is determined by the `dst`'s "earlier sibling". Here is an example 
// to illustrate the idea.
// 
// Hypothesis:
//   - node `s` defined in stage `x`;
//   - `s` has 2 cross-stage users, `d1`, `d2`;
//   - users' stage numbers are all different: `y1`, `y2`
//   - Total area: Area(chunk(s) + d1 + d2) + lambda * (y2 - x) * BitCount(s)
// Discussion about remat's effect:
//   - only do remat on edge "x -> d1":
//     - save (y1 - x) * BitCount(s)
//     - d2 can reuse `x`'s remat in stage y1, total Area will be:
//       - Area(2 * chunk(s) + d1 + d2) + lambda * (y2 - y1) * BitCount(s)
//   - only do remat on edge "x -> d2":
//     - save (y2 - y1) * BitCount(s)
//     - d1 still has to load value from registers, total Area will be:
//       - Area(2 * chunk(s) + d1 + d2) + lambda * (y1 - x) * BitCount(s)
int64_t RematReducedStageNumber(Node* src, Node* dst, 
    const absl::flat_hash_map<Node*, int64_t>& schedule) {
  XLS_CHECK(src->HasUser(dst));

  // sort users by decending scheduling stage nubmer

  std::vector<Node*> users_ordered;
  users_ordered.insert(users_ordered.begin(), 
      src->users().begin(), src->users().end());
  std::sort(users_ordered.begin(), users_ordered.end(), [&](Node* n1, Node* n2){
    return schedule.at(n1) > schedule.at(n2);
  });

  // find the user whose stage-number is just early than `dst`'s 

  int64_t dst_stage = schedule.at(dst);
  auto it = std::find_if(users_ordered.begin(), users_ordered.end(), 
      [&](Node* node) {
    return schedule.at(node) < dst_stage;
  });

  int64_t src_stage = schedule.at(src);
  return it == users_ordered.end() ? 
      src_stage - dst_stage /* no earlier sibling */:
      schedule.at(*it) - dst_stage /* has earlier user */;
}

// Helpers to clone all nodes in the chunk.
// Traverse the computation graph by topological order.
absl::StatusOr<absl::flat_hash_map<Node*, Node*>>
CloneDeadNodeChunk(FunctionBase* f, const absl::flat_hash_set<Node*>& chunk){
  // TODO(zihao): `topo_index` may be buffered to avoid recomputing
  absl::flat_hash_map<Node*, int64_t> topo_index;
  {
    std::vector<Node*> topo_sort = TopoSort(f).AsVector();
    for (int64_t i = 0; i < topo_sort.size(); ++i) {
      topo_index[topo_sort[i]] = i;
    }
  }
  
  std::vector<Node*> chunk_toposorted(chunk.begin(), chunk.end());
  std::stable_sort(chunk_toposorted.begin(), chunk_toposorted.end(),
                  [&](Node* x, Node* y) -> bool {
                    return topo_index.at(x) < topo_index.at(y);
                  });
  
  absl::flat_hash_map<Node*, Node*> clones;
  for (Node* node : chunk_toposorted) {
    std::vector<Node*> cloned_operands;
    for (Node* operand : node->operands()) {
      cloned_operands.push_back(clones.at(operand));
    }
    XLS_ASSIGN_OR_RETURN(clones[node], node->Clone(cloned_operands));
  }
  return clones;
}

// Available value, i.e., still stored in the input register.
// A value is available at stage `k` if it is defined in stage `a`
// and its last use is in stage `b` and a ≤ k ≤ b.
absl::flat_hash_set<Node*> AvaiableNodesAtStage(FunctionBase* f,
    const absl::flat_hash_map<Node*, int64_t>& schedule, int64_t stage) {
  absl::flat_hash_set<Node*> available;
  for (Node* node: f->nodes()) {
    // [start, end] is `node`'s live range
    int64_t start = schedule.at(node);
    int64_t end = start;
    for (Node* user : node->users()) {
      end = std::max(end, schedule.at(user));
    }

    if (start <= stage && stage <= end) {
      available.insert(node);
    }
  }
  return available;
}

// Currently returns only the maximal rematerialization at the given node
// (i.e.: the one that rematerializes _all_ incoming edges from previous
// pipeline stages).
absl::StatusOr<std::vector<RematOpportunity>>
FindRematerializationOpportunitiesAtNode(
    Node* target, FunctionBase* f,
    const absl::flat_hash_map<Node*, int64_t>& schedule,
    const InverseSchedule& inverse_schedule) {
  absl::flat_hash_set<Node*> available = AvaiableNodesAtStage(f, schedule, schedule.at(target));

  // TODO: remove nodes that would make the schedule worse from `available`

  absl::flat_hash_set<Node*> unavailable;
  for (Node* node : TopoSort(f)) {
    if (!available.contains(node)) {
      unavailable.insert(node);
    }
  }

  absl::flat_hash_map<Node*, absl::flat_hash_set<Node*>> replacements;
  absl::flat_hash_map<Node*, Node*> child_clones;
  for (Node* child : target->operands()) {
    if (schedule.at(child) < schedule.at(target)) {
      absl::flat_hash_set<Node*> chunk =
          ComputeDeadNodeChunk(unavailable, child);
      // If `chunk` were empty, that would mean that `child` is available, but
      // we just checked for this in the surrounding `if` statement.
      XLS_CHECK(!chunk.empty());
      if (std::any_of(chunk.begin(), chunk.end(),
                      [](Node* node) { return node->operands().empty(); })) {
        // TODO: later consider not clone the entire chunk. Maybe use a min-cut, 
        // which is related to the 3rd recompute method in the issue.
        continue;
      }
      replacements[child] = chunk;
      XLS_ASSIGN_OR_RETURN(auto clones, CloneDeadNodeChunk(f, chunk));
      child_clones[child] = clones.at(child);
    }
  }

  if (replacements.empty()) {
    return std::vector<RematOpportunity>();
  }
  
  std::vector<Node*> cloned_target_operands;
  for (int64_t i = 0; i < target->operands().size(); ++i) {
    Node* child = target->operands()[i];
    cloned_target_operands.push_back(
        (schedule.at(child) < schedule.at(target)) ? child_clones.at(child) : child);
  }

  XLS_ASSIGN_OR_RETURN(Node * target_replacement,
                       target->Clone(cloned_target_operands));

  double quality = 0.0;

  for (Node* child : target->operands()) {
    quality += RematReducedStageNumber(child, target, schedule) * 
        child->GetType()->GetFlatBitCount();
  }

  constexpr double area_per_flop = 10.0;

  quality *= area_per_flop;

  return std::vector<RematOpportunity>{
      RematOpportunity{target, target_replacement, quality}};
}

// Apply static analyses to find opportunities using techniques 1, 2, and 3
// May include optimization each opportunity in isolation, or optimizing them
// jointly.
// This will mutate `f` by adding lots of dead nodes.
// No two opportunities may have the same `to_rematerialize` field.
// All nodes added for a given `rematerialization` will be assumed to be in the
// same pipeline stage as the `to_rematerialize` node.
absl::StatusOr<std::vector<RematOpportunity>>
FindRematerializationOpportunities(
    FunctionBase* f, const absl::flat_hash_map<Node*, int64_t>& schedule,
    const DelayEstimator& delay_estimator) {
  InverseSchedule inverse_schedule = InvertSchedule(schedule);
  std::vector<RematOpportunity> result;
  for (Node* target : TopoSort(f)) {
    // We only want to rematerialize nodes if they have incoming edges from
    // previous pipeline stages.
    bool has_incoming_edges = false;
    for (Node* source : target->operands()) {
      if (schedule.at(source) < schedule.at(target)) {
        has_incoming_edges = true;
      }
    }
    if (has_incoming_edges) {
      XLS_ASSIGN_OR_RETURN(std::vector<RematOpportunity> opportunities_at_node,
                           FindRematerializationOpportunitiesAtNode(
                               target, f, schedule, inverse_schedule));
      if (!opportunities_at_node.empty()) {
        // Currently just take the first opportunity given, if there is one.
        // Eventually this will grow into a smarter heuristic for choosing
        // between mutually exclusive opportunities at a given edge.
        result.push_back(opportunities_at_node[0]);
      }
    }
  }

  return result;
}

// TODO(zihao): replace `FindRematerializationOpportunitiesAtNode` later
absl::StatusOr<absl::optional<RematEdgeOpportunity>>
FindRematerializationOpportunitiesAtEdge(
    Node* src, Node* dst, FunctionBase* f,
    const absl::flat_hash_map<Node*, int64_t>& schedule,
    const InverseSchedule& inverse_schedule) {
  int64_t dst_stage_num = schedule.at(dst);
  absl::flat_hash_set<Node*> available = AvaiableNodesAtStage(f, schedule, dst_stage_num);

  // TODO: remove nodes that would make the schedule worse from `available`

  absl::flat_hash_set<Node*> unavailable;
  for(int64_t i = 0; i < dst_stage_num; ++i) {
    for(const auto& node : inverse_schedule[i]){
      unavailable.insert(node);
    }
  }

  absl::flat_hash_set<Node*> chunk = ComputeDeadNodeChunk(unavailable, src);
  if(chunk.empty() ||
     std::any_of(chunk.begin(), chunk.end(),
                  [](Node* node) { return node->operands().empty(); })){
    // TODO: later consider not clone the entire chunk. Maybe use a min-cut, 
    // which is related to the 3rd recompute method in the issue.
    return absl::nullopt;
  }

  XLS_ASSIGN_OR_RETURN(auto clones, CloneDeadNodeChunk(f, chunk));

  double quality = RematReducedStageNumber(src, dst, schedule) * 
      src->GetType()->GetFlatBitCount();

  constexpr double area_per_flop = 10.0;

  quality *= area_per_flop;

  return RematEdgeOpportunity{src, dst, clones.at(src), quality};
}

// TODO(zihao): replace `FindRematerializationOpportunities` later
absl::StatusOr<std::vector<RematEdgeOpportunity>>
FindRematerializationEdgeOpportunities(
    FunctionBase* f, const absl::flat_hash_map<Node*, int64_t>& schedule,
    const DelayEstimator& delay_estimator) {
  InverseSchedule inverse_schedule = InvertSchedule(schedule);
  std::vector<RematEdgeOpportunity> result;
  for (Node* target : TopoSort(f)) {
    // We only want to rematerialize nodes if they have incoming edges from
    // previous pipeline stages.
    for (Node* source : target->operands()) {
      if (schedule.at(source) < schedule.at(target)) {
        XLS_ASSIGN_OR_RETURN(absl::optional<RematEdgeOpportunity> opportunity_at_edge,
                          FindRematerializationOpportunitiesAtEdge(
                            source, target, f, schedule, inverse_schedule));
        if (opportunity_at_edge.has_value()) { 
          result.push_back(opportunity_at_edge.value());
        }
      }
    }
  }
  return result;
}

// Computes the area of the given node, not including its operands.
double AreaOfNode(Node* node) {
  return node->GetType()->GetFlatBitCount() * 2.0;
}

absl::StatusOr<bool> Rematerialization(
    FunctionBase* f, absl::flat_hash_map<Node*, int64_t>* schedule,
    const DelayEstimator& delay_estimator) {
  PassOptions options;
  PassResults results;

  bool modified = false;

  // Quit early if there is only one pipeline stage.

  if (NumberOfPipelineStages(*schedule) <= 1) {
    return false;
  }

  // Ensure there are no dead nodes to begin with.

  {
    absl::flat_hash_set<Node*> deleted;
    XLS_ASSIGN_OR_RETURN(bool pass_modified, RunDce(f, false, &deleted));
    modified |= pass_modified;

    // Update the schedule.
    for (Node* node : deleted) {
      schedule->erase(node);
    }
  }

  // Ensure CSE later won't merge `to_rematerialize` nodes.

  {
    absl::flat_hash_map<Node*, Node*> replacements;
    XLS_ASSIGN_OR_RETURN(bool pass_modified,
                         RunCse(f, &replacements, *schedule));
    modified |= pass_modified;
  }

  // Calculate the set of nodes before rematerialization opportunity nodes were
  // added, so we can subtract them out later to determine which nodes were
  // added when `FindRematerializationOpportunities` is called.

  absl::flat_hash_set<Node*> original_nodes;
  for (Node* node : TopoSort(f)) {
    original_nodes.insert(node);
  }

  // Compute the set of rematerialization opportunities.

  XLS_ASSIGN_OR_RETURN(
      std::vector<RematOpportunity> opportunities,
      FindRematerializationOpportunities(f, *schedule, delay_estimator));

  // Fast path to quit if no rematerialization opportunities were found.

  if (opportunities.empty()) {
    return modified;
  }

  // Update the schedule with the stages of nodes added by calling
  // `FindRematerializationOpportunities`. This is necessary so that when we
  // call the CSE pass, we can pass in the schedule ensuring that nodes are only
  // shared between rematerializations in the same stage.
  //
  // Currently we assume that any nodes added for a given rematerialization
  // will be in the same stage as the node that is being rematerialized.
  // This will probably change at some point, as we implement more sophisticated
  // ways of finding opportunities.

  {
    // Nodes added when `FindRematerializationOpportunity` was called.
    absl::flat_hash_set<Node*> added;

    // If a node isn't in `original_nodes`, it must have been added.
    for (Node* node : TopoSort(f)) {
      if (!original_nodes.contains(node)) {
        added.insert(node);
      }
    }

    // Compute longest paths and slack so that we can prune opportunities that
    // negatively affect the critical path.
    XLS_ASSIGN_OR_RETURN(LongestPathLength longest,
                         LongestNodePaths(f, delay_estimator));
    absl::flat_hash_map<Node*, Slack> slack =
        ComputeSlack(f, InvertSchedule(*schedule), longest);

    std::vector<RematOpportunity> unpruned_opportunities;

    for (const RematOpportunity& opportunity : opportunities) {
      int64_t stage = schedule->at(opportunity.to_rematerialize);
      absl::flat_hash_set<Node*> chunk =
          ComputeDeadNodeChunk(added, opportunity.rematerialization);

      // Compute the delay of the replacement
      Delay replacement_delay = 0;
      for (Node* node : chunk) {
        replacement_delay =
            std::max(replacement_delay,
                     longest.at(node).at(opportunity.rematerialization));
      }

      // Determine if the replacement will lengthen the critical path through
      // this stage. If so, skip over this opportunity.
      Slack s = slack.at(opportunity.to_rematerialize);
      if (replacement_delay + s.longest_path_out_of > s.stage_critical_path) {
        XLS_VLOG(3) << "Opportunity was pruned: "
                    << opportunity.to_rematerialize;
        continue;
      }

      // Update the schedule.
      for (Node* node : chunk) {
        (*schedule)[node] = stage;
      }

      // This opportunity wasn't pruned, so save it
      unpruned_opportunities.push_back(opportunity);
    }

    // Remove all unpruned opportunities
    opportunities = unpruned_opportunities;
  }

  // We can run CSE _once_ and it will merge together all the dead nodes
  // created for the rematerialization opportunities, and then we can later use
  // `ComputeDeadNodeChunk` to find the nodes that actually end up getting added
  // for a given subset of the opportunities.

  {
    absl::flat_hash_map<Node*, Node*> replacements;
    XLS_ASSIGN_OR_RETURN(bool pass_modified,
                         RunCse(f, &replacements, *schedule));
    modified |= pass_modified;
    // If CSE merged together two `opportunity.rematerialization`, rewrite them
    // to the merged node.
    for (RematOpportunity& opportunity : opportunities) {
      if (replacements.contains(opportunity.rematerialization)) {
        opportunity.rematerialization =
            replacements.at(opportunity.rematerialization);
      }
    }
  }

  // Compute the set of dead nodes for use in `ComputeDeadNodeChunk`.

  absl::flat_hash_set<Node*> dead;
  XLS_RETURN_IF_ERROR(RunDce(f, true, &dead).status());

  // Define the submodular cost function.

  SubmodularFunction<RematOpportunity> objective(
      /*universe=*/absl::btree_set<RematOpportunity>(opportunities.begin(),
                                                     opportunities.end()),
      /*function=*/
      [&](const absl::btree_set<RematOpportunity>& chosen) -> double {
        // Compute the set of nodes added by all of the chosen opportunities.
        absl::flat_hash_set<Node*> added_nodes;
        for (const RematOpportunity& opportunity : chosen) {
          for (Node* added_node :
               ComputeDeadNodeChunk(dead, opportunity.rematerialization)) {
            added_nodes.insert(added_node);
          }
        }

        // Add the area of all the added nodes to the result.
        double result = 0.0;
        for (Node* added_node : added_nodes) {
          result += AreaOfNode(added_node);
        }

        // Subtract the quality of all the chosen opportunities from the result.
        for (const RematOpportunity& opportunity : chosen) {
          result -= opportunity.quality;
        }

        return result;
      });

  // Minimize the submodular cost function.

  MinimizeOptions minimize_options{MinimizeMode::Alternating, std::nullopt,
                                   100};
  absl::btree_set<RematOpportunity> chosen =
      objective.ApproxMinimize(minimize_options);

  // For any selected rematerialization opportunity, apply it by replacing uses
  // of the node with its rematerialization.

  for (const RematOpportunity& opportunity : chosen) {
    XLS_VLOG(3) << "Applying rematerialization opportunity:\n"
                << "  node = " << opportunity.to_rematerialize << "\n"
                << "  qual = " << opportunity.quality << "\n";

    XLS_RETURN_IF_ERROR(opportunity.to_rematerialize->ReplaceUsesWith(
        opportunity.rematerialization));
  }

  // Delete any rematerialization opportunity nodes that were not chosen.

  {
    absl::flat_hash_set<Node*> deleted;
    XLS_ASSIGN_OR_RETURN(bool pass_modified, RunDce(f, false, &deleted));
    modified |= pass_modified;

    // Update the schedule.
    for (Node* node : deleted) {
      schedule->erase(node);
    }
  }

  return modified;
}

// TODO(zihao): replace `Rematerialization`
absl::StatusOr<bool> RematerializationReplacement(
    FunctionBase* f, absl::flat_hash_map<Node*, int64_t>* schedule,
    const DelayEstimator& delay_estimator) {
  PassOptions options;
  PassResults results;

  bool modified = false;

  // Quit early if there is only one pipeline stage.

  if (NumberOfPipelineStages(*schedule) <= 1) {
    return false;
  }

  // Ensure there are no dead nodes to begin with.

  {
    absl::flat_hash_set<Node*> deleted;
    XLS_ASSIGN_OR_RETURN(bool pass_modified, RunDce(f, false, &deleted));
    modified |= pass_modified;

    // Update the schedule.
    for (Node* node : deleted) {
      schedule->erase(node);
    }
  }

  // Ensure CSE later won't merge `to_rematerialize` nodes.

  {
    absl::flat_hash_map<Node*, Node*> replacements;
    XLS_ASSIGN_OR_RETURN(bool pass_modified,
                         RunCse(f, &replacements, *schedule));
    modified |= pass_modified;
  }

  // Calculate the set of nodes before rematerialization opportunity nodes were
  // added, so we can subtract them out later to determine which nodes were
  // added when `FindRematerializationEdgeOpportunities` is called.

  absl::flat_hash_set<Node*> original_nodes;
  for (Node* node : TopoSort(f)) {
    original_nodes.insert(node);
  }

  // Compute the set of rematerialization opportunities.

  XLS_ASSIGN_OR_RETURN(
      std::vector<RematEdgeOpportunity> opportunities,
      FindRematerializationEdgeOpportunities(f, *schedule, delay_estimator));

  // Fast path to quit if no rematerialization opportunities were found.

  if (opportunities.empty()) {
    return modified;
  }

  // Update the schedule with the stages of nodes added by calling
  // `FindRematerializationEdgeOpportunities`. This is necessary so that when we
  // call the CSE pass, we can pass in the schedule ensuring that nodes are only
  // shared between rematerializations in the same stage.
  //
  // Currently we assume that any nodes added for a given rematerialization
  // will be in the same stage as the node that is being rematerialized.
  // This will probably change at some point, as we implement more sophisticated
  // ways of finding opportunities.

  {
    // Nodes added when `FindRematerializationOpportunity` was called.
    absl::flat_hash_set<Node*> added;

    // If a node isn't in `original_nodes`, it must have been added.
    for (Node* node : TopoSort(f)) {
      if (!original_nodes.contains(node)) {
        added.insert(node);
      }
    }

    // Compute longest paths and slack so that we can prune opportunities that
    // negatively affect the critical path.
    XLS_ASSIGN_OR_RETURN(LongestPathLength longest,
                         LongestNodePaths(f, delay_estimator));
    absl::flat_hash_map<Node*, Slack> slack =
        ComputeSlack(f, InvertSchedule(*schedule), longest);

    std::vector<RematEdgeOpportunity> unpruned_opportunities;

    for (const RematEdgeOpportunity& opportunity : opportunities) {
      int64_t stage = schedule->at(opportunity.edge_dst);
      absl::flat_hash_set<Node*> chunk =
          ComputeDeadNodeChunk(added, opportunity.rematerialization);

      // Compute the delay of the replacement
      Delay replacement_delay = 0;
      for (Node* node : chunk) {
        replacement_delay =
            std::max(replacement_delay,
                     longest.at(node).at(opportunity.rematerialization));
      }

      // Determine if the replacement will lengthen the critical path through
      // this stage. If so, skip over this opportunity.
      Slack s = slack.at(opportunity.edge_dst);
      if (replacement_delay + s.longest_path_out_of > s.stage_critical_path) {
        XLS_VLOG(3) << "Opportunity was pruned: "
                    << opportunity.edge_src << "->"
                    << opportunity.edge_dst;
        continue;
      }

      // Update the schedule.
      for (Node* node : chunk) {
        (*schedule)[node] = stage;
      }

      // This opportunity wasn't pruned, so save it
      unpruned_opportunities.push_back(opportunity);
    }

    // Remove all unpruned opportunities
    opportunities = unpruned_opportunities;
  }

  // We can run CSE _once_ and it will merge together all the dead nodes
  // created for the rematerialization opportunities, and then we can later use
  // `ComputeDeadNodeChunk` to find the nodes that actually end up getting added
  // for a given subset of the opportunities.

  {
    absl::flat_hash_map<Node*, Node*> replacements;
    XLS_ASSIGN_OR_RETURN(bool pass_modified,
                         RunCse(f, &replacements, *schedule));
    modified |= pass_modified;
    // If CSE merged together two `opportunity.rematerialization`, rewrite them
    // to the merged node.
    for (RematEdgeOpportunity& opportunity : opportunities) {
      if (replacements.contains(opportunity.rematerialization)) {
        opportunity.rematerialization =
            replacements.at(opportunity.rematerialization);
      }
    }
  }

  // Compute the set of dead nodes for use in `ComputeDeadNodeChunk`.

  absl::flat_hash_set<Node*> dead;
  XLS_RETURN_IF_ERROR(RunDce(f, true, &dead).status());

  // Define the submodular cost function.

  SubmodularFunction<RematEdgeOpportunity> objective(
      /*universe=*/absl::btree_set<RematEdgeOpportunity>(opportunities.begin(),
                                                     opportunities.end()),
      /*function=*/
      [&](const absl::btree_set<RematEdgeOpportunity>& chosen) -> double {
        // Compute the set of nodes added by all of the chosen opportunities.
        absl::flat_hash_set<Node*> added_nodes;
        for (const RematEdgeOpportunity& opportunity : chosen) {
          for (Node* added_node :
               ComputeDeadNodeChunk(dead, opportunity.rematerialization)) {
            added_nodes.insert(added_node);
          }
        }

        // Add the area of all the added nodes to the result.
        double result = 0.0;
        for (Node* added_node : added_nodes) {
          result += AreaOfNode(added_node);
        }

        // Subtract the quality of all the chosen opportunities from the result.
        for (const RematEdgeOpportunity& opportunity : chosen) {
          result -= opportunity.quality;
        }

        return result;
      });

  // Minimize the submodular cost function.

  MinimizeOptions minimize_options{MinimizeMode::Alternating, std::nullopt,
                                   100};
  absl::btree_set<RematEdgeOpportunity> chosen =
      objective.ApproxMinimize(minimize_options);

  // For any selected rematerialization opportunity, apply it by replacing uses
  // of the node with its rematerialization.

  for (const RematEdgeOpportunity& opportunity : chosen) {
    XLS_VLOG(3) << "Applying rematerialization opportunity:\n"
                << "  edge = (" << opportunity.edge_src
                << ", " << opportunity.edge_dst << ")\n"
                << "  qual = " << opportunity.quality << "\n";

    XLS_CHECK(opportunity.edge_dst->ReplaceOperand(
      opportunity.edge_src,
      opportunity.rematerialization));
  }

  // Delete any rematerialization opportunity nodes that were not chosen.

  {
    absl::flat_hash_set<Node*> deleted;
    XLS_ASSIGN_OR_RETURN(bool pass_modified, RunDce(f, false, &deleted));
    modified |= pass_modified;

    // Update the schedule.
    for (Node* node : deleted) {
      schedule->erase(node);
    }
  }

  return modified;
}

// Use ValueNumbering method to find equivalence classes.
// 
// It is able to find more hidden common sub-expression, 
// which may expose wider range of opportunities:
//  1. to use near-by equivalent nodes when searching the `DeadNodeChunk`.
//  2. Dead nodes introduced by remat opportunities in the same stage, 
//     may have more chances to share nodes aross different chunks.
//  3. maybe other ...
// 
// This implementation heavily references `cse_pass`'s. The main difference is 
// that the hash method use operands' ValueNumber rather than node-id.
// 
// Although inspired by `cse_pass`, it is not proposed to replace it. 
// It is not always beneficial to eliminate all the CSE, because it may introduce 
// dense data dependence edges, which may have a nagetive effect on scheduling.
// `literal_uncommoning_pass` is a good example.
//
// TODO: 
//   - just as a placeholder, `dry_run` on cse_pass will save a lot of code
//   - A possible opportunity, may not be useful:
//     - First, find all the equivalence class.
//     - Traverse each cross-stage DU edge. If the defined value has an equivalence 
//       value in later stage, this USE can be replaced to the later one. It can 
//       reduce register pressure.
// Comment(zihao): 
//   It seems that this function has the equal ability with "cse_pass" to 
//   find equivalence value. The only difference is that "cse_pass" will 
//   merge all subexprs, while this function only FINDs the equivalence. 
//   I am not sure whether it is still useful.
absl::StatusOr<absl::flat_hash_map<Node*, int64_t>>
FindValueNumberingEquivClasses(FunctionBase* f){
  absl::flat_hash_map<Node*, int64_t> vn_tbl;

  auto assign_vn = [&](Node* node, int64_t vn){
    vn_tbl[node] = vn;
  };

  auto query_vn = [&](Node* node){
    return vn_tbl.at(node);
  };
  
  // Initialize nodes' Value Number to "-1". Later, we will
  // use it to check whether a node has a value number.
  for(Node* node : f->nodes()){
    if (OpIsSideEffecting(node->op())) {
      continue;
    }
    vn_tbl[node] = -1;
  }

  int64_t next_vn = 0;

  // Handle other nodes.
  {
    // Returns the operands of the given node for the purposes of the
    // int64_t analysis. The operands may be reordered, because 
    // of the propery of commutative for some specific kind of op. 
    // This lambda function heavily references `GetOperandsForCse` 
    // function defined in `cse_pass`.
    auto get_hash_elements = [&](Node* node, 
        std::vector<int64_t>* operands_vn_store) 
        -> absl::Span<int64_t> {
      XLS_CHECK(operands_vn_store->empty());
      operands_vn_store->push_back(static_cast<int64_t>(node->op()));
      operands_vn_store->push_back(node->BitCountOrDie());
      if (!OpIsCommutative(node->op())) {
        std::transform(
          node->operands().begin(), node->operands().end(), 
          std::back_inserter(*operands_vn_store), query_vn);
        return absl::Span<int64_t>{*operands_vn_store};
      }

      // reorder operands by value number

      std::vector<Node*> operands_store;
      operands_store.insert(operands_store.begin(),
          node->operands().begin(), node->operands().end());
      std::sort(operands_store.begin(), operands_store.end(), 
          [&](Node* n1, Node* n2){
        return query_vn(n1) < query_vn(n2);
      });

      std::transform(operands_store.begin(), operands_store.end(),
        std::back_inserter(*operands_vn_store), query_vn);
      return absl::Span<int64_t>{*operands_vn_store};
    };
    
    auto hasher = absl::Hash<std::vector<int64_t>>();
    auto node_hash = [&](Node* n) {
      std::vector<int64_t> values_to_hash;
      get_hash_elements(n, &values_to_hash);
      return hasher(values_to_hash);
    };

    // The following loop is very similar to the main-loop in 
    // `RunCse` defined in `cse_pass`.

    absl::flat_hash_map<int64_t, std::vector<Node*>> node_buckets;
    node_buckets.reserve(f->node_count());
    for (Node* node : TopoSort(f)) {
      if (OpIsSideEffecting(node->op())) {
        continue;
      }

      int64_t hash = node_hash(node);
      node_buckets[hash].push_back(node);

      XLS_CHECK(query_vn(node) == -1);

      // Assign value number to this node.

      bool found_equiv = false;

      if (node_buckets.contains(hash)) {
        std::vector<int64_t> node_span_backing_store;
        absl::Span<int64_t> node_hash_elems = 
            get_hash_elements(node, &node_span_backing_store);
        for (Node* candidate : node_buckets.at(hash)) {
          std::vector<int64_t> candidate_span_backing_store;
          if (node_hash_elems ==
                  get_hash_elements(candidate, &candidate_span_backing_store) && 
              node->IsDefinitelyEqualTo(candidate)) {
            // Find an equivalent node. Assign the old value number.
            assign_vn(node, query_vn(candidate));
            found_equiv = true;
            break;
          }
        }
      }

      if(!found_equiv){
        // There is no equivalent node. Assign a new value number.
        assign_vn(node, next_vn++);
      }
    }
  }
  return vn_tbl;
}

}  // namespace xls
