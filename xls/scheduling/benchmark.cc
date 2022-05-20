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

#include <numeric>
#include <iostream>
#include <ctime>
#include <algorithm>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_format.h"
#include "xls/common/file/filesystem.h"
#include "xls/common/init_xls.h"
#include "xls/common/logging/logging.h"
#include "xls/common/status/status_macros.h"
#include "xls/ir/ir_parser.h"
#include "xls/ir/node_iterator.h"
#include "xls/scheduling/pipeline_schedule.h"


#include "xls/delay_model/analyze_critical_path.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "xls/delay_model/delay_estimator.h"
#include "xls/delay_model/delay_estimators.h"
#include "xls/ir/node.h"
#include "xls/ir/op.h"
#include "xls/ir/type.h"

#include "xls/examples/schedule_benchmark_packages.h"

const char kUsage[] = R"(
Expected invocation:
  benchmark_main
Example invocation:
  benchmark_main
)";

namespace xls {
namespace {

class TestDelayEstimator : public DelayEstimator {
 public:
  TestDelayEstimator() : DelayEstimator("test") {}

  absl::StatusOr<int64_t> GetOperationDelayInPs(Node* node) const override {
    switch (node->op()) {
      case Op::kAfterAll:
      case Op::kBitSlice:
      case Op::kConcat:
      case Op::kLiteral:
      case Op::kParam:
      case Op::kReceive:
      case Op::kSend:
      case Op::kTupleIndex:
        return 0;
      case Op::kUDiv:
      case Op::kSDiv:
        return 3;
      case Op::kUMul:
      case Op::kSMul:
        return 2;
      default:
        return 1;
    }
  }
};

absl::Status RealMain() {
  // Compute the minimum cost partition of each benchmark and validate the
  // results.
  XLS_ASSIGN_OR_RETURN(std::vector<std::string> benchmark_names,
                           sample_packages::GetBenchmarkNames());

  std::vector<std::string> sample_results;
  for (const std::string& benchmark_name : benchmark_names) {
    XLS_ASSIGN_OR_RETURN(
        std::unique_ptr<Package> p,
        sample_packages::GetBenchmark(benchmark_name, /*optimized=*/true));
    absl::StatusOr<Function*> f_status = p->GetTopAsFunction();
    if (!f_status.ok()) {
      // Skip packages which need the entry to be specified explicitly.
      continue;
    }

    Function * f = f_status.value();

    XLS_ASSIGN_OR_RETURN(std::vector<CriticalPathEntry> critical_path, AnalyzeCriticalPath(f, absl::nullopt, TestDelayEstimator()));
    int64_t max_delay = critical_path.front().path_delay_ps;
    
    std::vector<std::string> data_points;
    std::vector<double> boost_ratio;
    //int64_t clk_min = std::max((int64_t)3, max_delay/4 - 1);
    //int64_t clk_max = std::min((int64_t)clk_min + 8, max_delay/2);
    int64_t clk_min = 3;
    int64_t clk_max = max_delay/2;
    for(int64_t clk = clk_min;
        clk < clk_max;
        clk += 1){
      SchedulingOptions sdc_exact_options(SchedulingStrategy::MINIMIZE_REGISTERS_SDC);
      SchedulingOptions cut_options(SchedulingStrategy::MINIMIZE_REGISTERS);
      cut_options.clock_period_ps(clk);
      sdc_exact_options.clock_period_ps(clk);

      clock_t t = clock();
      XLS_ASSIGN_OR_RETURN(
          PipelineSchedule sdc_exact_schedule,
          PipelineSchedule::Run(f, TestDelayEstimator(), sdc_exact_options));
      double sdc_work_time = (clock() - t) / double(CLOCKS_PER_SEC);
      if(sdc_work_time > 300){
          break;
      }

      t = clock();
      XLS_ASSIGN_OR_RETURN(
          PipelineSchedule cut_schedule,
          PipelineSchedule::Run(f, TestDelayEstimator(), cut_options));
      double cut_work_time = (clock() - t) / double(CLOCKS_PER_SEC);

      int64_t result_sdc = sdc_exact_schedule.CountFinalInteriorPipelineRegisters();
      int64_t result_cut = cut_schedule.CountFinalInteriorPipelineRegisters();
      XLS_CHECK_LE(result_sdc, result_cut);

      double boost = (result_cut - result_sdc)/(double)result_sdc;
      
      boost_ratio.push_back(boost);
      data_points.push_back(absl::StrFormat("{\"clk\":%lld, \"time_sdc\":%lf, \"time_cut\":%lf, \"result_sdc\":%lld, \"result_cut\":%lld, \"boost\":%lf}", 
                       clk, sdc_work_time, cut_work_time, result_sdc, result_cut, boost));
    
      std::cerr << benchmark_name << ":\n\t" 
                << data_points.back() << std::endl;
    }
    double average_boost_ratio = std::accumulate(boost_ratio.begin(), boost_ratio.end(), 0.0) / (double) boost_ratio.size();
    int64_t edge_count = std::accumulate(f->nodes().begin(), f->nodes().end(), (int64_t)0, [](int64_t acc, Node* node){
        return acc + node->users().size();
    });

    sample_results.push_back(absl::StrFormat("{\"name\":%s, \"max_delay\":%lld, \"node_count\":%lld, \"edge_count\":%lld, \"avg_boost\":%lf, \"data\" : [%s]}", benchmark_name, max_delay, f->node_count(), edge_count, average_boost_ratio, absl::StrJoin(data_points, ",")));

    std::cerr << "====\n" << sample_results.back() << "\n====\n" << std::endl;
  }

  std::cout << absl::StrFormat("[%s]", absl::StrJoin(sample_results, ",")) << std::endl;
  return absl::OkStatus();
}

}  // namespace
}  // namespace xls

int main(int argc, char** argv) {
  std::vector<absl::string_view> positional_arguments =
      xls::InitXls(kUsage, argc, argv);
    
  XLS_QCHECK_OK(xls::RealMain());
  return EXIT_SUCCESS;
}
