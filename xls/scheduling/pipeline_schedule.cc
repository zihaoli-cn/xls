// Copyright 2020 The XLS Authors
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
//#define LOG_SDC_INTERNAL_RUNNING_TIME_TO_CERR
#ifdef LOG_SDC_INTERNAL_RUNNING_TIME_TO_CERR
#include <iostream>
#include <ctime>
#endif
#include <algorithm>
#include <cmath>
#include <functional>
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "ortools/glop/lp_solver.h"
#include "ortools/lp_data/lp_data.h"
#include "ortools/lp_data/lp_types.h"
#include "xls/common/logging/log_lines.h"
#include "xls/common/logging/logging.h"
#include "xls/common/status/ret_check.h"
#include "xls/data_structures/binary_search.h"
#include "xls/ir/node_iterator.h"
#include "xls/ir/node_util.h"
#include "xls/scheduling/function_partition.h"
#include "xls/scheduling/pipeline_schedule.h"
#include "xls/scheduling/schedule_bounds.h"

namespace xls {
namespace {

// Returns the largest cycle value to which any node is mapped in the given
// ScheduleCycleMap. This is the maximum value of any value element in the map.
int64_t MaximumCycle(const ScheduleCycleMap& cycle_map) {
  int64_t max_cycle = 0;
  for (const auto& pair : cycle_map) {
    max_cycle = std::max(max_cycle, pair.second);
  }
  return max_cycle;
}

// Splits the nodes at the boundary between 'cycle' and 'cycle + 1' by
// performing a minimum cost cut and tightens the bounds accordingly. Upon
// return no node in the function will have a range which spans both 'cycle' and
// 'cycle + 1'.
absl::Status SplitAfterCycle(FunctionBase* f, int64_t cycle,
                             const DelayEstimator& delay_estimator,
                             sched::ScheduleBounds* bounds) {
  XLS_VLOG(3) << "Splitting after cycle " << cycle;

  // The nodes which need to be partitioned are those which can be scheduled in
  // either 'cycle' or 'cycle + 1'.
  std::vector<Node*> partitionable_nodes;
  for (Node* node : f->nodes()) {
    if (bounds->lb(node) <= cycle && bounds->ub(node) >= cycle + 1) {
      partitionable_nodes.push_back(node);
    }
  }

  std::pair<std::vector<Node*>, std::vector<Node*>> partitions =
      sched::MinCostFunctionPartition(f, partitionable_nodes);

  // Tighten bounds based on the cut.
  for (Node* node : partitions.first) {
    XLS_RETURN_IF_ERROR(bounds->TightenNodeUb(node, cycle));
  }
  for (Node* node : partitions.second) {
    XLS_RETURN_IF_ERROR(bounds->TightenNodeLb(node, cycle + 1));
  }

  return absl::OkStatus();
}

// Returns the number of pipeline registers (flops) on the interior of the
// pipeline not counting the input and output flops (if any).
absl::StatusOr<int64_t> CountInteriorPipelineRegisters(
    FunctionBase* f, const sched::ScheduleBounds& bounds) {
  int64_t registers = 0;
  for (Node* node : f->nodes()) {
    XLS_RET_CHECK_EQ(bounds.lb(node), bounds.ub(node)) << absl::StrFormat(
        "%s [%d, %d]", node->GetName(), bounds.lb(node), bounds.ub(node));
    int64_t latest_use = bounds.lb(node);
    for (Node* user : node->users()) {
      latest_use = std::max(latest_use, bounds.lb(user));
    }
    registers +=
        node->GetType()->GetFlatBitCount() * (latest_use - bounds.lb(node));
  }
  return registers;
}

// Schedules the given function into a pipeline with the given clock
// period. Attempts to split nodes into stages such that the total number of
// flops in the pipeline stages is minimized without violating the target clock
// period.
absl::StatusOr<ScheduleCycleMap> ScheduleToMinimizeRegisters(
    FunctionBase* f, int64_t pipeline_stages,
    const DelayEstimator& delay_estimator, sched::ScheduleBounds* bounds) {
  XLS_VLOG(3) << "ScheduleToMinimizeRegisters()";
  XLS_VLOG(3) << "  pipeline stages = " << pipeline_stages;
  XLS_VLOG_LINES(4, f->DumpIr());

  XLS_VLOG(4) << "Initial bounds:";
  XLS_VLOG_LINES(4, bounds->ToString());

  // Try a number of different orderings of cycle boundary at which the min-cut
  // is performed and keep the best one.
  int64_t best_register_count = std::numeric_limits<int64_t>::max();
  absl::optional<sched::ScheduleBounds> best_bounds;
  for (const std::vector<int64_t>& cut_order :
       GetMinCutCycleOrders(pipeline_stages - 1)) {
    XLS_VLOG(3) << absl::StreamFormat("Trying cycle order: {%s}",
                                      absl::StrJoin(cut_order, ", "));
    sched::ScheduleBounds trial_bounds = *bounds;
    // Partition the nodes at each cycle boundary. For each iteration, this
    // splits the nodes into those which must be scheduled at or before the
    // cycle and those which must be scheduled after. Upon loop completion each
    // node will have a range of exactly one cycle.
    for (int64_t cycle : cut_order) {
      XLS_RETURN_IF_ERROR(
          SplitAfterCycle(f, cycle, delay_estimator, &trial_bounds));
      XLS_RETURN_IF_ERROR(trial_bounds.PropagateLowerBounds());
      XLS_RETURN_IF_ERROR(trial_bounds.PropagateUpperBounds());
    }
    XLS_ASSIGN_OR_RETURN(int64_t trial_register_count,
                         CountInteriorPipelineRegisters(f, trial_bounds));
    if (!best_bounds.has_value() ||
        best_register_count > trial_register_count) {
      best_bounds = std::move(trial_bounds);
      best_register_count = trial_register_count;
    }
  }
  *bounds = std::move(*best_bounds);

  ScheduleCycleMap cycle_map;
  for (Node* node : f->nodes()) {
    XLS_RET_CHECK_EQ(bounds->lb(node), bounds->ub(node)) << node->GetName();
    cycle_map[node] = bounds->lb(node);
  }
  return cycle_map;
}

using DelayMap = absl::flat_hash_map<Node*, int64_t>;
// A helper function to compute each node's delay by calling the delay estimator
// The result is used by `ComputeCriticalCombPathsUntilNextCycle`,
// `ComputeSingleSourceCCPUntilNextCycle`,
// `ScheduleToMinimizeRegistersSDC`
absl::StatusOr<DelayMap> ComputeNodeDelays(
    FunctionBase* f, const DelayEstimator& delay_estimator) {
  DelayMap result;
  for (Node* node : f->nodes()) {
    XLS_ASSIGN_OR_RETURN(result[node],
                         delay_estimator.GetOperationDelayInPs(node));
  }
  return result;
}

// Search a those paths whose combinational delay exceeds clock period
// The result only contains all the destinations by `ScheduleToMinimizeRegistersSDC`
std::vector<Node*> SearchPathsJustExceedClockPeriod(Node* src, int64_t clock_period_ps, const DelayMap& delay_map){
  std::vector<Node*> result;
  std::function<void (Node*, int64_t)> dfs;

  dfs = [&](Node* node, int64_t node_start_time)->void{
    const int64_t node_end_time = node_start_time + delay_map.at(node);
    if(node_end_time > clock_period_ps){
      result.push_back(node);
      return;
    }
    for(Node* user : node->users()){
      dfs(user, node_end_time);
    }
  };

  dfs(src, 0);
  return result;
}

// Schedule to minimize the total pipeline registers using SDC scheduling
// the constraint matrix is totally unimodular, this ILP problem can be solved
// by LP.
// Reference:
//   - Cong, Jason, and Zhiru Zhang. "An efficient and versatile scheduling
//   algorithm based on SDC formulation." 2006 43rd ACM/IEEE Design Automation
//   Conference. IEEE, 2006.
//   - Zhang, Zhiru, and Bin Liu. "SDC-based modulo scheduling for pipeline
//   synthesis." 2013 IEEE/ACM International Conference on Computer-Aided Design
//   (ICCAD). IEEE, 2013.
absl::StatusOr<ScheduleCycleMap> ScheduleToMinimizeRegistersSDC(
    FunctionBase* f, int64_t pipeline_stages,
    const DelayEstimator& delay_estimator, sched::ScheduleBounds* bounds,
    int64_t clock_period_ps) {
  XLS_VLOG(3) << "ScheduleToMinimizeRegistersSDC()";
  XLS_VLOG(3) << "  pipeline stages = " << pipeline_stages;
  XLS_VLOG_LINES(4, f->DumpIr());

  XLS_VLOG(4) << "Initial bounds:";
  XLS_VLOG_LINES(4, bounds->ToString());

  namespace or_tools = ::operations_research::glop;
  or_tools::LinearProgram lp;
  const double infinity = or_tools::kInfinity;

  // Node's cycle after scheduling
  absl::flat_hash_map<Node*, or_tools::ColIndex> cycle_var;

  // Node's lifetime, from when it finishes executing 
  //   until it is consumed by the last user
  absl::flat_hash_map<Node*, or_tools::ColIndex> lifetime_var;
  for (Node* node : f->nodes()) {
    cycle_var[node] = lp.CreateNewVariable();
    lp.SetVariableBounds(cycle_var[node], bounds->lb(node),
                         bounds->ub(node));

    lifetime_var[node] = lp.CreateNewVariable();
    lp.SetVariableBounds(lifetime_var[node], 0.0, bounds->max_lower_bound());
  }

  // a dummy node to represent an artificial sink node on the data-dependence graph
  or_tools::ColIndex cycle_at_sinknode = lp.CreateNewVariable();

  for (Node* node : f->nodes()) {
    or_tools::ColIndex lifetime_at_node = lifetime_var[node];
    or_tools::ColIndex cycle_at_node = cycle_var[node];

    auto add_du_chains_related_constraints = [&lp, infinity, &lifetime_at_node, &cycle_at_node](or_tools::ColIndex cycle_at_user){
      // Constraint: cycle[node] - cycle[node_user] <= 0
      or_tools::RowIndex constraint1 = lp.CreateNewConstraint();
      lp.SetConstraintBounds(constraint1, -infinity, 0.0);
      lp.SetCoefficient(constraint1, cycle_at_node, 1);
      lp.SetCoefficient(constraint1, cycle_at_user, -1);

      // Constraint: cycle[node_user] - cycle[node] - lifetime[node] <= 0
      or_tools::RowIndex constraint2 = lp.CreateNewConstraint();
      lp.SetConstraintBounds(constraint2, -infinity, 0.0);
      lp.SetCoefficient(constraint2, cycle_at_user, 1);
      lp.SetCoefficient(constraint2, cycle_at_node, -1);
      lp.SetCoefficient(constraint2, lifetime_at_node, -1);
    };

    for (Node* user : node->users()) {
      add_du_chains_related_constraints(cycle_var.at(user));
    }
    if (f->HasImplicitUse(node)) {
      add_du_chains_related_constraints(cycle_at_sinknode);
    }
  }

#ifdef LOG_SDC_INTERNAL_RUNNING_TIME_TO_CERR
  clock_t t = clock();
#endif
  XLS_ASSIGN_OR_RETURN(auto delay_map, ComputeNodeDelays(f, delay_estimator));
  for (Node* src : f->nodes()){
    for (Node* dst : SearchPathsJustExceedClockPeriod(src, clock_period_ps, delay_map)){
      // Constraint: cycle[path_dst] - cycle[path_src] >= 1
      or_tools::RowIndex constraint = lp.CreateNewConstraint();
      lp.SetConstraintBounds(constraint, 1, infinity);
      lp.SetCoefficient(constraint, cycle_var[dst], 1);
      lp.SetCoefficient(constraint, cycle_var[src], -1);
    }
  }
#ifdef LOG_SDC_INTERNAL_RUNNING_TIME_TO_CERR
  std::cerr << absl::StrFormat("%.4e", (clock() - t) / double(CLOCKS_PER_SEC)) << ",";
#endif

  for (Node* node : f->nodes()) {
    lp.SetObjectiveCoefficient(lifetime_var[node],
                               node->GetType()->GetFlatBitCount());
  }
  lp.SetMaximizationProblem(false);
  lp.CleanUp();

  or_tools::LPSolver solver;
  or_tools::GlopParameters parameters;
  parameters.set_provide_strong_optimal_guarantee(true);
  solver.SetParameters(parameters);

#ifdef LOG_SDC_INTERNAL_RUNNING_TIME_TO_CERR
  t = clock();
#endif
  or_tools::ProblemStatus status = solver.Solve(lp);
#ifdef LOG_SDC_INTERNAL_RUNNING_TIME_TO_CERR
  std::cerr << absl::StrFormat("%.4e", (clock() - t) / double(CLOCKS_PER_SEC)) << std::endl;
#endif

  // Check that the problem has an optimal solution.
  if (status != or_tools::ProblemStatus::OPTIMAL) {
    return absl::InternalError(
        "The problem does not have an optimal solution!");
  }

  // extract result
  const or_tools::DenseRow& ans = solver.variable_values();
  ScheduleCycleMap cycle_map;
  for (Node* node : f->nodes()) {
    double cycle = ans[cycle_var[node]];
    if (std::fabs(cycle - std::round(cycle)) > 1e-3) {
      return absl::InternalError(
          "The scheduling result is expected to be integer");
    }
    cycle_map[node] = cycle;
  }
  return cycle_map;
}

// Returns the nodes of `f` which must be scheduled in the first stage of a
// pipeline. For functions this is parameters. For procs, this is receive nodes
// and next state nodes.
// TODO(meheff): 2021/09/22 Enable receives to be scheduled in cycles other than
// the initial cycle.
std::vector<Node*> FirstStageNodes(FunctionBase* f) {
  if (Function* function = dynamic_cast<Function*>(f)) {
    return std::vector<Node*>(function->params().begin(),
                              function->params().end());
  }
  if (Proc* proc = dynamic_cast<Proc*>(f)) {
    std::vector<Node*> nodes(proc->params().begin(), proc->params().end());
    for (Node* node : proc->nodes()) {
      // TODO(tedhong): 2021/10/14 Make this more flexible (ex. for ii>N),
      // where the next state node must be scheduled before a specific state
      // but not necessarily the 1st stage.
      if (node->Is<Receive>() ||
          std::find(proc->NextState().begin(), proc->NextState().end(), node) !=
              proc->NextState().end()) {
        nodes.push_back(node);
      }
    }
    return nodes;
  }

  return {};
}

// Returns the nodes of `f` which must be scheduled in the final stage of a
// pipeline. For functions this is the return value. For procs, this is send
// nodes.
// TODO(meheff): 2021/09/22 Enable sends to be scheduled in cycles other than
// the final cycle.
std::vector<Node*> FinalStageNodes(FunctionBase* f) {
  if (Function* function = dynamic_cast<Function*>(f)) {
    // If the return value is a parameter, then we do not force the return value
    // to be scheduled in the final stage because, as a parameter, the node must
    // be in the first stage.
    if (function->return_value()->Is<Param>()) {
      return {};
    }
    return {function->return_value()};
  }
  if (Proc* proc = dynamic_cast<Proc*>(f)) {
    std::vector<Node*> nodes;
    for (Node* node : proc->nodes()) {
      if (node->Is<Send>()) {
        nodes.push_back(node);
      }
    }
    return nodes;
  }

  return {};
}

// Construct ScheduleBounds for the given function assuming the given
// clock period and delay estimator. `topo_sort` should be a topological sort of
// the nodes of `f`. If `schedule_length` is given then the upper bounds are
// set on the bounds object with the maximum upper bound set to
// `schedule_length` - 1. Otherwise, the maximum upper bound is set to the
// maximum lower bound.
absl::StatusOr<sched::ScheduleBounds> ConstructBounds(
    FunctionBase* f, int64_t clock_period_ps, std::vector<Node*> topo_sort,
    absl::optional<int64_t> schedule_length,
    const DelayEstimator& delay_estimator) {
  sched::ScheduleBounds bounds(f, std::move(topo_sort), clock_period_ps,
                               delay_estimator);

  // Initially compute the lower bounds of all nodes.
  XLS_RETURN_IF_ERROR(bounds.PropagateLowerBounds());

  int64_t upper_bound;
  if (schedule_length.has_value()) {
    if (schedule_length.value() <= bounds.max_lower_bound()) {
      return absl::ResourceExhaustedError(absl::StrFormat(
          "Cannot be scheduled in %d stages. Computed lower bound is %d.",
          schedule_length.value(), bounds.max_lower_bound() + 1));
    }
    upper_bound = schedule_length.value() - 1;
  } else {
    upper_bound = bounds.max_lower_bound();
  }

  // Set the lower bound of nodes which must be in the final stage to
  // `upper_bound`
  bool rerun_lb_propagation = false;
  for (Node* node : FinalStageNodes(f)) {
    if (bounds.lb(node) != upper_bound) {
      XLS_RETURN_IF_ERROR(bounds.TightenNodeLb(node, upper_bound));
      if (!node->users().empty()) {
        rerun_lb_propagation = true;
      }
    }
  }
  // If fixing nodes in the final stage changed any lower bounds then
  // repropagate the lower bounds.
  if (rerun_lb_propagation) {
    XLS_RETURN_IF_ERROR(bounds.PropagateLowerBounds());
  }

  if (bounds.max_lower_bound() > upper_bound) {
    return absl::ResourceExhaustedError(absl::StrFormat(
        "Impossible to schedule Function/Proc %s; the following "
        "node(s) must be scheduled in the final cycle but that "
        "is impossible due to users of these node(s): %s",
        f->name(), absl::StrJoin(FinalStageNodes(f), ", ", NodeFormatter)));
  }

  // Set and propagate upper bounds.
  for (Node* node : f->nodes()) {
    XLS_RETURN_IF_ERROR(bounds.TightenNodeUb(node, upper_bound));
  }
  for (Node* node : FirstStageNodes(f)) {
    if (bounds.lb(node) > 0) {
      return absl::ResourceExhaustedError(
          absl::StrFormat("Impossible to schedule Function/Proc %s; node `%s` "
                          "must be scheduled in the first cycle but that is "
                          "impossible due to the node's operand(s)",
                          f->name(), node->GetName()));
    }
    XLS_RETURN_IF_ERROR(bounds.TightenNodeUb(node, 0));
  }
  XLS_RETURN_IF_ERROR(bounds.PropagateUpperBounds());

  return std::move(bounds);
}

// Returns the critical path through the given nodes (ordered topologically).
absl::StatusOr<int64_t> ComputeCriticalPath(
    absl::Span<Node* const> topo_sort, const DelayEstimator& delay_estimator) {
  int64_t function_cp = 0;
  absl::flat_hash_map<Node*, int64_t> node_cp;
  for (Node* node : topo_sort) {
    int64_t node_start = 0;
    for (Node* operand : node->operands()) {
      node_start = std::max(node_start, node_cp[operand]);
    }
    XLS_ASSIGN_OR_RETURN(int64_t node_delay,
                         delay_estimator.GetOperationDelayInPs(node));
    node_cp[node] = node_start + node_delay;
    function_cp = std::max(function_cp, node_cp[node]);
  }
  return function_cp;
}

// Returns the minimum clock period in picoseconds for which it is feasible to
// schedule the function into a pipeline with the given number of stages.
absl::StatusOr<int64_t> FindMinimumClockPeriod(
    FunctionBase* f, int64_t pipeline_stages,
    const DelayEstimator& delay_estimator) {
  XLS_VLOG(4) << "FindMinimumClockPeriod()";
  XLS_VLOG(4) << "  pipeline stages = " << pipeline_stages;
  auto topo_sort_it = TopoSort(f);
  std::vector<Node*> topo_sort(topo_sort_it.begin(), topo_sort_it.end());
  XLS_ASSIGN_OR_RETURN(int64_t function_cp,
                       ComputeCriticalPath(topo_sort, delay_estimator));
  // The lower bound of the search is the critical path delay evenly distributed
  // across all stages (rounded up), and the upper bound is simply the critical
  // path of the entire function. It's possible this upper bound is the best you
  // can do if there exists a single operation with delay equal to the
  // critical-path delay of the function.
  int64_t search_start = (function_cp + pipeline_stages - 1) / pipeline_stages;
  int64_t search_end = function_cp;
  XLS_VLOG(4) << absl::StreamFormat("Binary searching over interval [%d, %d]",
                                    search_start, search_end);
  XLS_ASSIGN_OR_RETURN(
      int64_t min_period,
      BinarySearchMinTrueWithStatus(
          search_start, search_end,
          [&](int64_t clk_period_ps) -> absl::StatusOr<bool> {
            absl::StatusOr<sched::ScheduleBounds> bounds_or = ConstructBounds(
                f, clk_period_ps, topo_sort,
                /*schedule_length=*/absl::nullopt, delay_estimator);
            if (!bounds_or.ok()) {
              return false;
            }
            return bounds_or.value().max_lower_bound() < pipeline_stages;
          }));
  XLS_VLOG(4) << "minimum clock period = " << min_period;

  return min_period;
}

// Returns a sequence of numbers from first to last where the zeroth element of
// the sequence is the middle element between first and last. Subsequent
// elements are selected recursively out of the two intervals before and after
// the middle element.
std::vector<int64_t> MiddleFirstOrder(int64_t first, int64_t last) {
  if (first == last) {
    return {first};
  }
  if (first == last - 1) {
    return {first, last};
  }

  int64_t middle = (first + last) / 2;
  std::vector<int64_t> head = MiddleFirstOrder(first, middle - 1);
  std::vector<int64_t> tail = MiddleFirstOrder(middle + 1, last);

  std::vector<int64_t> ret;
  ret.push_back(middle);
  ret.insert(ret.end(), head.begin(), head.end());
  ret.insert(ret.end(), tail.begin(), tail.end());
  return ret;
}

}  // namespace

std::vector<std::vector<int64_t>> GetMinCutCycleOrders(int64_t length) {
  if (length == 0) {
    return {{}};
  }
  if (length == 1) {
    return {{0}};
  }
  if (length == 2) {
    return {{0, 1}, {1, 0}};
  }
  // For lengths greater than 2, return forward, reverse and middle first
  // orderings.
  std::vector<std::vector<int64_t>> orders;
  std::vector<int64_t> forward(length);
  std::iota(forward.begin(), forward.end(), 0);
  orders.push_back(forward);

  std::vector<int64_t> reverse(length);
  std::iota(reverse.begin(), reverse.end(), 0);
  std::reverse(reverse.begin(), reverse.end());
  orders.push_back(reverse);

  orders.push_back(MiddleFirstOrder(0, length - 1));
  return orders;
}

PipelineSchedule::PipelineSchedule(FunctionBase* function_base,
                                   ScheduleCycleMap cycle_map,
                                   absl::optional<int64_t> length)
    : function_base_(function_base), cycle_map_(std::move(cycle_map)) {
  // Build the mapping from cycle to the vector of nodes in that cycle.
  int64_t max_cycle = MaximumCycle(cycle_map_);
  if (length.has_value()) {
    XLS_CHECK_GT(*length, max_cycle);
    max_cycle = *length - 1;
  }
  // max_cycle is the latest cycle in which any node is scheduled so add one to
  // get the capacity because cycle numbers start at zero.
  cycle_to_nodes_.resize(max_cycle + 1);
  for (const auto& pair : cycle_map_) {
    Node* node = pair.first;
    int64_t cycle = pair.second;
    cycle_to_nodes_[cycle].push_back(node);
  }
  // The nodes in each cycle held in cycle_to_nodes_ must be in a topological
  // sort order.
  absl::flat_hash_map<Node*, int64_t> node_to_topo_index;
  int64_t i = 0;
  for (Node* node : TopoSort(function_base)) {
    node_to_topo_index[node] = i;
    ++i;
  }
  for (std::vector<Node*>& nodes_in_cycle : cycle_to_nodes_) {
    std::sort(nodes_in_cycle.begin(), nodes_in_cycle.end(),
              [&](Node* a, Node* b) {
                return node_to_topo_index[a] < node_to_topo_index[b];
              });
  }
}

absl::StatusOr<PipelineSchedule> PipelineSchedule::FromProto(
    Function* function, const PipelineScheduleProto& proto) {
  ScheduleCycleMap cycle_map;
  for (const auto& stage : proto.stages()) {
    for (const auto& node_name : stage.nodes()) {
      XLS_ASSIGN_OR_RETURN(Node * node, function->GetNode(node_name));
      cycle_map[node] = stage.stage();
    }
  }
  return PipelineSchedule(function, cycle_map);
}

absl::Span<Node* const> PipelineSchedule::nodes_in_cycle(int64_t cycle) const {
  if (cycle < cycle_to_nodes_.size()) {
    return cycle_to_nodes_[cycle];
  }
  return absl::Span<Node* const>();
}

std::vector<Node*> PipelineSchedule::GetLiveOutOfCycle(int64_t c) const {
  std::vector<Node*> live_out;
  for (int64_t i = 0; i <= c; ++i) {
    for (Node* node : nodes_in_cycle(i)) {
      if (node->function_base()->HasImplicitUse(node) ||
          absl::c_any_of(node->users(),
                         [&](Node* u) { return cycle(u) > c; })) {
        live_out.push_back(node);
      }
    }
  }
  return live_out;
}

namespace {
class DelayEstimatorWithInputDelay : public DelayEstimator {
 public:
  DelayEstimatorWithInputDelay(const DelayEstimator& base, int64_t input_delay)
      : DelayEstimator(absl::StrFormat("%s_with_input_delay", base.name())),
        base_delay_estimator_(&base),
        input_delay_(input_delay) {}

  virtual absl::StatusOr<int64_t> GetOperationDelayInPs(
      Node* node) const override {
    XLS_ASSIGN_OR_RETURN(int64_t base_delay,
                         base_delay_estimator_->GetOperationDelayInPs(node));

    return (node->op() == Op::kReceive) ? base_delay + input_delay_
                                        : base_delay;
  }

 private:
  const DelayEstimator* base_delay_estimator_;
  int64_t input_delay_;
};
}  // namespace

/*static*/ absl::StatusOr<PipelineSchedule> PipelineSchedule::Run(
    FunctionBase* f, const DelayEstimator& delay_estimator,
    const SchedulingOptions& options) {
  int64_t input_delay = options.additional_input_delay_ps().has_value()
                            ? options.additional_input_delay_ps().value()
                            : 0;

  DelayEstimatorWithInputDelay delay_estimator_with_delay(delay_estimator,
                                                          input_delay);

  int64_t clock_period_ps;
  if (options.clock_period_ps().has_value()) {
    clock_period_ps = *options.clock_period_ps();

    if (options.clock_margin_percent().has_value()) {
      int64_t original_clock_period_ps = clock_period_ps;
      clock_period_ps -=
          (clock_period_ps * options.clock_margin_percent().value() + 50) / 100;
      if (clock_period_ps <= 0) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Clock period non-positive (%dps) after adjusting for margin. "
            "Original clock period: %dps, clock margin: %d%%",
            clock_period_ps, original_clock_period_ps,
            *options.clock_margin_percent()));
      }
    }
  } else {
    XLS_RET_CHECK(options.pipeline_stages().has_value());
    // A pipeline length is specified, but no target clock period. Determine
    // the minimum clock period for which the function can be scheduled in the
    // given pipeline length.
    XLS_ASSIGN_OR_RETURN(clock_period_ps,
                         FindMinimumClockPeriod(f, *options.pipeline_stages(),
                                                delay_estimator_with_delay));

    if (options.period_relaxation_percent().has_value()) {
      int64_t relaxation_percent = options.period_relaxation_percent().value();

      clock_period_ps += (clock_period_ps * relaxation_percent + 50) / 100;
    }
  }

  XLS_ASSIGN_OR_RETURN(
      sched::ScheduleBounds bounds,
      ConstructBounds(f, clock_period_ps, TopoSort(f).AsVector(),
                      options.pipeline_stages(), delay_estimator_with_delay));
  int64_t schedule_length = bounds.max_lower_bound() + 1;

  ScheduleCycleMap cycle_map;
  if (options.strategy() == SchedulingStrategy::MINIMIZE_REGISTERS) {
    XLS_ASSIGN_OR_RETURN(cycle_map, ScheduleToMinimizeRegisters(
                                        f, schedule_length,
                                        delay_estimator_with_delay, &bounds));
  } else if (options.strategy() == SchedulingStrategy::MINIMIZE_REGISTERS_SDC) {
    XLS_ASSIGN_OR_RETURN(
        cycle_map, ScheduleToMinimizeRegistersSDC(f, schedule_length,
                                                  delay_estimator_with_delay,
                                                  &bounds, clock_period_ps));
  } else {
    XLS_RET_CHECK(options.strategy() == SchedulingStrategy::ASAP);
    XLS_RET_CHECK(!options.pipeline_stages().has_value());
    // Just schedule everything as soon as possible.
    for (Node* node : f->nodes()) {
      cycle_map[node] = bounds.lb(node);
    }
  }
  auto schedule = PipelineSchedule(f, cycle_map, options.pipeline_stages());
  XLS_RETURN_IF_ERROR(
      schedule.VerifyTiming(clock_period_ps, delay_estimator_with_delay));
  XLS_VLOG_LINES(3, "Schedule\n" + schedule.ToString());
  return schedule;
}

std::string PipelineSchedule::ToString() const {
  absl::flat_hash_map<const Node*, int64_t> topo_pos;
  int64_t pos = 0;
  for (Node* node : TopoSort(function_base_)) {
    topo_pos[node] = pos;
    pos++;
  }

  std::string result;
  for (int64_t cycle = 0; cycle <= length(); ++cycle) {
    absl::StrAppendFormat(&result, "Cycle %d:\n", cycle);
    // Emit nodes in topo-sort order for easier reading.
    std::vector<Node*> nodes(nodes_in_cycle(cycle).begin(),
                             nodes_in_cycle(cycle).end());
    std::sort(nodes.begin(), nodes.end(), [&](Node* a, Node* b) {
      return topo_pos.at(a) < topo_pos.at(b);
    });
    for (Node* node : nodes) {
      absl::StrAppendFormat(&result, "  %s\n", node->ToString());
    }
  }
  return result;
}

absl::Status PipelineSchedule::Verify() const {
  for (Node* node : function_base()->nodes()) {
    XLS_RET_CHECK(IsScheduled(node));
  }
  for (Node* node : function_base()->nodes()) {
    for (Node* operand : node->operands()) {
      XLS_RET_CHECK_LE(cycle(operand), cycle(node));
    }
  }

  // Verify initial nodes in cycle 0. Final nodes in final cycle.
  return absl::OkStatus();
}

absl::Status PipelineSchedule::VerifyTiming(
    int64_t clock_period_ps, const DelayEstimator& delay_estimator) const {
  // Critical path from start of the cycle that a node is scheduled through the
  // node itself. If the schedule meets timing, then this value should be less
  // than or equal to clock_period_ps for every node.
  absl::flat_hash_map<Node*, int64_t> node_cp;
  // The predecessor (operand) of the node through which the critical-path from
  // the start of the cycle extends.
  absl::flat_hash_map<Node*, Node*> cp_pred;
  // The node with the longest critical path from the start of the stage in the
  // entire schedule.
  Node* max_cp_node = nullptr;
  for (Node* node : TopoSort(function_base_)) {
    // The critical-path delay from the start of the stage to the start of the
    // node.
    int64_t cp_to_node_start = 0;
    cp_pred[node] = nullptr;
    for (Node* operand : node->operands()) {
      if (cycle(operand) == cycle(node)) {
        if (cp_to_node_start < node_cp.at(operand)) {
          cp_to_node_start = node_cp.at(operand);
          cp_pred[node] = operand;
        }
      }
    }
    XLS_ASSIGN_OR_RETURN(int64_t node_delay,
                         delay_estimator.GetOperationDelayInPs(node));
    node_cp[node] = cp_to_node_start + node_delay;
    if (max_cp_node == nullptr || node_cp[node] > node_cp[max_cp_node]) {
      max_cp_node = node;
    }
  }

  if (node_cp[max_cp_node] > clock_period_ps) {
    std::vector<Node*> path;
    Node* node = max_cp_node;
    do {
      path.push_back(node);
      node = cp_pred[node];
    } while (node != nullptr);
    std::reverse(path.begin(), path.end());
    return absl::InternalError(absl::StrFormat(
        "Schedule does not meet timing (%dps). Longest failing path (%dps): %s",
        clock_period_ps, node_cp[max_cp_node],
        absl::StrJoin(path, " -> ", [&](std::string* out, Node* n) {
          absl::StrAppend(
              out, absl::StrFormat(
                       "%s (%dps)", n->GetName(),
                       delay_estimator.GetOperationDelayInPs(n).value()));
        })));
  }
  return absl::OkStatus();
}

PipelineScheduleProto PipelineSchedule::ToProto() const {
  PipelineScheduleProto proto;
  proto.set_function(function_base_->name());
  for (int i = 0; i < cycle_to_nodes_.size(); i++) {
    StageProto* stage = proto.add_stages();
    stage->set_stage(i);
    for (const Node* node : cycle_to_nodes_[i]) {
      stage->add_nodes(node->GetName());
    }
  }
  return proto;
}

int64_t PipelineSchedule::CountFinalInteriorPipelineRegisters() const {
  int64_t reg_count = 0;

  Function* as_func = dynamic_cast<Function*>(function_base_);
  for (int64_t stage = 0; stage < length(); ++stage) {
    for (Node* function_base_node : function_base_->nodes()) {
      if (cycle(function_base_node) > stage) {
        continue;
      }

      auto is_live_out_of_stage = [&](Node* n) {
        if (stage == length() - 1) {
          return false;
        }
        if (as_func && (n == as_func->return_value())) {
          return true;
        }
        for (Node* user : n->users()) {
          if (cycle(user) > stage) {
            return true;
          }
        }
        return false;
      };

      if (is_live_out_of_stage(function_base_node)) {
        reg_count += function_base_node->GetType()->GetFlatBitCount();
      }
    }
  }

  return reg_count;
}

}  // namespace xls
