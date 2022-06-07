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
//#define ENABLE_LOG_TO_CERR
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
    std::vector<double> boost_ratios;
    std::vector<int64_t> reduced_register_widths;

    int64_t clk_min = 3;
    int64_t clk_max = max_delay/2;
    double sdc_work_time = 0.0;
    for(int64_t clk = clk_min;
        clk < clk_max;
        clk += 1){
      if(sdc_work_time > 200){
          break;
      }
      SchedulingOptions sdc_exact_options(SchedulingStrategy::MINIMIZE_REGISTERS_SDC);
      SchedulingOptions sdc_exact_fast_options(SchedulingStrategy::MINIMIZE_REGISTERS_SDC_FAST);
      SchedulingOptions cut_options(SchedulingStrategy::MINIMIZE_REGISTERS);
      sdc_exact_options.clock_period_ps(clk);
      sdc_exact_fast_options.clock_period_ps(clk);
      cut_options.clock_period_ps(clk);

      clock_t t = clock();
      XLS_ASSIGN_OR_RETURN(
          PipelineSchedule sdc_exact_schedule,
          PipelineSchedule::Run(f, TestDelayEstimator(), sdc_exact_options));
      sdc_work_time = (clock() - t) / double(CLOCKS_PER_SEC);

      t = clock();
      XLS_ASSIGN_OR_RETURN(
          PipelineSchedule sdc_exact_fast_schedule,
          PipelineSchedule::Run(f, TestDelayEstimator(), sdc_exact_fast_options));
      double sdc_fast_work_time = (clock() - t) / double(CLOCKS_PER_SEC);

      t = clock();
      XLS_ASSIGN_OR_RETURN(
          PipelineSchedule cut_schedule,
          PipelineSchedule::Run(f, TestDelayEstimator(), cut_options));
      double cut_work_time = (clock() - t) / double(CLOCKS_PER_SEC);

      int64_t bits_sdc = sdc_exact_schedule.CountFinalInteriorPipelineRegisters();
      int64_t bits_sdc_fast = sdc_exact_fast_schedule.CountFinalInteriorPipelineRegisters();
      int64_t bits_min_cut = cut_schedule.CountFinalInteriorPipelineRegisters();
      XLS_CHECK_LE(bits_sdc, bits_min_cut);
      XLS_CHECK_EQ(bits_sdc_fast, bits_sdc);

      int64_t reduced_register_width = bits_min_cut - bits_sdc_fast;
      double boost = (double)reduced_register_width/(double)bits_min_cut;
      
      boost_ratios.push_back(boost);
      reduced_register_widths.push_back(reduced_register_width);
      data_points.push_back(absl::StrFormat("{\"clk\":%lld, \"t_sdc\":%.4e, \"t_sdc_fast\":%.4e, \"t_min_cut\":%.4e, \"bits_sdc\":%lld, \"bits_sdc_fast\":%lld, \"bits_min_cut\":%lld, \"reduced_bits\":%lld, \"boost\":%.4e}", 
                       clk, sdc_work_time, sdc_fast_work_time, cut_work_time, bits_sdc, bits_sdc_fast, bits_min_cut, reduced_register_width, boost));

#ifdef ENABLE_LOG_TO_CERR
      std::cerr << benchmark_name << ":\n\t" 
                << data_points.back() << std::endl;
#endif
    }
    double avg_ratio = std::accumulate(boost_ratios.begin(), boost_ratios.end(), 0.0) / (double) boost_ratios.size();
    int64_t avg_reduced_width = std::accumulate(reduced_register_widths.begin(), reduced_register_widths.end(), (int64_t)0) / reduced_register_widths.size();
    int64_t edge_count = std::accumulate(f->nodes().begin(), f->nodes().end(), (int64_t)0, [](int64_t acc, Node* node){
        return acc + node->users().size();
    });

    sample_results.push_back(absl::StrFormat("{\"name\":\"%s\", \"cp\":%lld, \"nodes\":%lld, \"edges\":%lld, \"avg\":%.4e, \"avg_raw\":%lld, \"data\" : [%s]}", benchmark_name, max_delay, f->node_count(), edge_count, avg_ratio, avg_reduced_width, absl::StrJoin(data_points, ",")));

#ifdef ENABLE_LOG_TO_CERR
    std::cerr << "====\n" << sample_results.back() << "\n====\n" << std::endl;
#endif
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
