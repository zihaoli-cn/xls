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

#include "xls/passes/rematerialization_pass.h"

#include "absl/hash/hash.h"
#include "absl/status/statusor.h"
#include "xls/common/logging/logging.h"
#include "xls/common/status/status_macros.h"
#include "xls/data_structures/submodular.h"
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
    absl::flat_hash_set<Node*> entry_nodes;
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

      int64_t entry_to_node_max = 0;
      for (Node* entry : entry_nodes) {
        if (longest.at(entry).contains(node)) {
          entry_to_node_max =
              std::max(entry_to_node_max, longest.at(entry).at(node));
          path_exists = true;
        }
      }

      int64_t node_to_exit_max = 0;
      for (Node* entry : entry_nodes) {
        if (longest.at(entry).contains(node)) {
          node_to_exit_max =
              std::max(node_to_exit_max, longest.at(entry).at(node));
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

// Returns for a given dead node, the largest "contiguous" set of dead nodes
// that feed into this node.
// A node is within this set iff all paths starting at that node and ending
// at the given node only touch dead nodes, and at least one such path exists.
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

// Currently returns only the maximal rematerialization at the given node
// (i.e.: the one that rematerializes _all_ incoming edges from previous
// pipeline stages).
absl::StatusOr<std::vector<RematOpportunity>>
FindRematerializationOpportunitiesAtNode(
    Node* target, FunctionBase* f,
    const absl::flat_hash_map<Node*, int64_t>& schedule,
    const InverseSchedule& inverse_schedule) {
  absl::flat_hash_set<Node*> available;
  for (Node* node : inverse_schedule[schedule.at(target)]) {
    available.insert(node);
  }

  // TODO: remove nodes that would make the schedule worse from `available`

  absl::flat_hash_set<Node*> unavailable;
  for (Node* node : TopoSort(f)) {
    if (!available.contains(node)) {
      unavailable.insert(node);
    }
  }

  absl::flat_hash_map<Node*, absl::flat_hash_set<Node*>> replacements;
  for (Node* child : target->operands()) {
    if (schedule.at(child) < schedule.at(target)) {
      absl::flat_hash_set<Node*> chunk =
          ComputeDeadNodeChunk(unavailable, child);
      // If `chunk` were empty, that would mean that `child` is available, but
      // we just checked for this in the surrounding `if` statement.
      XLS_CHECK(!chunk.empty());
      if (std::any_of(chunk.begin(), chunk.end(),
                      [](Node* node) { return node->operands().empty(); })) {
        continue;
      }
      replacements[child] = chunk;
    }
  }

  if (replacements.empty()) {
    return std::vector<RematOpportunity>();
  }

  absl::flat_hash_map<Node*, int64_t> topo_index;

  {
    std::vector<Node*> topo_sort = TopoSort(f).AsVector();
    for (int64_t i = 0; i < topo_sort.size(); ++i) {
      topo_index[topo_sort[i]] = i;
    }
  }

  absl::flat_hash_map<Node*, Node*> clones;
  for (const auto& [child, chunk] : replacements) {
    std::vector<Node*> chunk_toposorted(chunk.begin(), chunk.end());
    std::stable_sort(chunk_toposorted.begin(), chunk_toposorted.end(),
                     [&](Node* x, Node* y) -> bool {
                       return topo_index.at(x) < topo_index.at(y);
                     });
    for (Node* node : chunk_toposorted) {
      std::vector<Node*> cloned_operands;
      for (Node* operand : node->operands()) {
        cloned_operands.push_back(clones.at(operand));
      }
      XLS_ASSIGN_OR_RETURN(clones[node], node->Clone(cloned_operands));
    }
  }

  std::vector<Node*> cloned_target_operands;
  for (int64_t i = 0; i < target->operands().size(); ++i) {
    Node* child = target->operands()[i];
    cloned_target_operands.push_back(
        (schedule.at(child) < schedule.at(target)) ? clones.at(child) : child);
  }

  XLS_ASSIGN_OR_RETURN(Node * target_replacement,
                       target->Clone(cloned_target_operands));

  double quality = 0.0;

  for (Node* child : target->operands()) {
    quality += (schedule.at(target) - schedule.at(child)) *
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

}  // namespace xls
