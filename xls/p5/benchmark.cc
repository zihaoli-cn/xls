#include "xls/p5/benchmark.h"

#include "absl/strings/str_format.h"

#include "xls/common/file/filesystem.h"
#include "xls/ir/function.h"
#include "xls/p5/ir_converter.h"
#include "xls/p5/json_ast_parser.h"
#include "xls/p5/util/load_json.h"

ABSL_FLAG(std::string, sched_benchmark_name_prefix, "bench",
          "P5 Json file's name prefix");

namespace xls::p5 {

absl::StatusOr<int32_t> CheckBenchmarkSize(const std::string &benchmark_dir,
                                           const std::string &prefix) {
  std::filesystem::path base = std::string(benchmark_dir);
  absl::Status status = absl::OkStatus();
  int32_t counter = 0;
  while (status.ok()) {
    status = FileExists(base / prefix / absl::StrFormat("%d.json", counter++));
  }
  if (status.ok())
    return counter - 1;
  else
    return status;
}

TranslationBenchmark::TranslationBenchmark(const std::string &benchmark_dir,
                                           const std::string &prefix,
                                           int32_t num_sample)
    : benchmark_dir_(benchmark_dir), prefix_(prefix), num_sample_(num_sample),
      current_idx_(0) {
  json_ast_.reserve(num_sample);
  cpp_ast_.reserve(num_sample);
  parser_duration_.reserve(num_sample);

  cpp_ast_size_.reserve(num_sample);

  transform_duration_.reserve(num_sample);

  active_cpp_ast_size1_.reserve(num_sample);

  analysis_duration_.reserve(num_sample);

  active_cpp_ast_size2_.reserve(num_sample);

  conversion_duration_.reserve(num_sample);

  ir_.reserve(num_sample);

  ir_nodes_.reserve(num_sample);
}

std::string TranslationBenchmark::GetCurrentFileName() const {
  std::filesystem::path path = benchmark_dir_;
  path /= absl::StrFormat("%s%d.json", prefix_, current_idx_);

  return path.string();
}

absl::Status TranslationBenchmark::Parse() {
  absl::Time start = absl::Now();

  XLS_ASSIGN_OR_RETURN(std::unique_ptr<nlohmann::json> json,
                       LoadJson(GetCurrentFileName()));
  XLS_ASSIGN_OR_RETURN(std::unique_ptr<Module> ast_module,
                       ParseModuleFromJson(*json));

  json_ast_.push_back(std::move(json));
  cpp_ast_.push_back(std::move(ast_module));
  parser_duration_.push_back(absl::Now() - start);
}

absl::Status TranslationBenchmark::Translate() {
  IrConverterProfiler profiler;
  XLS_ASSIGN_OR_RETURN(
      std::unique_ptr<Package> ir_package,
      ConvertP5AstModuleToPackage(cpp_ast_.back().get(), nullptr, &profiler));

  int64_t ir_counter = 0;
  for (auto &f : ir_package->functions()) {
    ir_counter += f->node_count();
  }

  cpp_ast_size_.push_back(profiler.active_ast_num1);
  transform_duration_.push_back(profiler.tranform_duration);
  active_cpp_ast_size1_.push_back(profiler.active_ast_num2);
  analysis_duration_.push_back(profiler.analysis_duration);
  active_cpp_ast_size2_.push_back(profiler.active_ast_num3);
  conversion_duration_.push_back(profiler.conversion_duration);
  ir_.push_back(std::move(ir_package));
  ir_nodes_.push_back(ir_counter);
}
} // namespace xls::p5