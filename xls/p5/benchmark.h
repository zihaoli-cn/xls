#pragma once

#include "absl/flags/declare.h"
#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"

#include "xls/delay_model/delay_estimator.h"
#include "xls/delay_model/delay_estimators.h"
#include "xls/ir/package.h"

#include "xls/p5/ast.h"
#include "xls/p5/util/json.hpp"
#include "xls/p5/util/load_json.h"

#include <string>

namespace xls::p5 {

class TestDelayEstimator : public DelayEstimator {
public:
  TestDelayEstimator() : DelayEstimator("test") {}

  absl::StatusOr<int64_t> GetOperationDelayInPs(Node *node) const override {
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

absl::StatusOr<int32_t> CheckBenchmarkSize(const std::string &benchmark_dir,
                                           const std::string &prefix);

class TranslationBenchmark {
public:
  TranslationBenchmark(const std::string &benchmark_dir,
                       const std::string &prefix, int32_t num_sample);

  absl::Status Run();

  std::string DumpJsonStatistics(bool print_vertical = true);

  // TODO
  //  std::string DumpJsonDepthSerise();

  std::string DumpHWTransResult(bool print_vertical = true);
  std::string DumpDataDepResult(bool print_vertical = true);

  absl::Status AnalyzeDataDep();

  absl::Span<std::unique_ptr<Package>> packages() {
    return absl::MakeSpan(ir_);
  }

private:
  std::string GetCurrentFileName() const;
  absl::Status Parse();
  absl::Status Translate();

  const std::string benchmark_dir_;
  const std::string prefix_;
  const int32_t num_sample_;

  int32_t current_idx_;

  std::vector<JsonProfiler> json_info_;

  std::vector<std::unique_ptr<nlohmann::json>> json_ast_;
  std::vector<std::unique_ptr<Module>> cpp_ast_;
  std::vector<absl::Duration> parser_duration_;

  std::vector<int64_t> cpp_ast_size_;

  std::vector<absl::Duration> transform_duration_;

  std::vector<int64_t> active_cpp_ast_size1_;

  std::vector<absl::Duration> analysis_duration_;

  std::vector<int64_t> active_cpp_ast_size2_;

  std::vector<absl::Duration> conversion_duration_;

  std::vector<std::unique_ptr<Package>> ir_;

  std::vector<absl::Duration> total_duration_;

  std::vector<int64_t> ir_nodes_;
  std::vector<int64_t> ir_edges_;
  std::vector<int64_t> ir_cp_nodes_;
  std::vector<int64_t> ir_cp_delay_;
  std::vector<int64_t> ir_params_;
  std::vector<int64_t> ir_params_bitcount_;
};

} // namespace xls::p5