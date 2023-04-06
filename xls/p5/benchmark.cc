#include "xls/p5/benchmark.h"

#include "absl/strings/str_format.h"
#include "absl/types/variant.h"

#include "xls/common/file/filesystem.h"
#include "xls/common/visitor.h"
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

  if (current_idx_ != json_ast_.size() || current_idx_ != cpp_ast_.size() ||
      current_idx_ != parser_duration_.size()) {
    return absl::InternalError("Invalid size");
  }

  json_ast_.push_back(std::move(json));
  cpp_ast_.push_back(std::move(ast_module));
  parser_duration_.push_back(absl::Now() - start);

  return absl::OkStatus();
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

  if (current_idx_ != cpp_ast_size_.size() ||
      current_idx_ != transform_duration_.size() ||
      current_idx_ != active_cpp_ast_size1_.size() ||
      current_idx_ != analysis_duration_.size() ||
      current_idx_ != active_cpp_ast_size2_.size() ||
      current_idx_ != conversion_duration_.size() ||
      current_idx_ != ir_.size() || current_idx_ != ir_nodes_.size()) {
    return absl::InternalError("Invalid size");
  }

  cpp_ast_size_.push_back(profiler.active_ast_num1);
  transform_duration_.push_back(profiler.tranform_duration);
  active_cpp_ast_size1_.push_back(profiler.active_ast_num2);
  analysis_duration_.push_back(profiler.analysis_duration);
  active_cpp_ast_size2_.push_back(profiler.active_ast_num3);
  conversion_duration_.push_back(profiler.conversion_duration);
  ir_.push_back(std::move(ir_package));
  ir_nodes_.push_back(ir_counter);

  return absl::OkStatus();
}

absl::Status TranslationBenchmark::Run() {
  for (int i = 0; i < num_sample_; ++i) {
    XLS_RETURN_IF_ERROR(Parse());
    XLS_RETURN_IF_ERROR(Translate());
    ++current_idx_;
  }

  return absl::OkStatus();
}

std::string DumpCSV(bool print_vertical = true) {
  std::string result;

  std::vector<std::string> headers = {"ID",          "Parser运行时间",
                                      "活跃AST数量", "Transformation运行时间",
                                      "活跃AST数量", "Analysis运行时间",
                                      "活跃AST数量", "Translation运行时间",
                                      "IR节点数量"};

  std::vector<int64_t> id_vec;
  id_vec.reserve(num_sample_);

  for (int i = 0; i < num_sample_; ++i) {
    id_vec.push_back(i);
  }

  using VectorPtrType =
      absl::variant<std::vector<absl::Duration> *, std::vector<int64_t> *>;

  std::vector<VectorPtrType> vector_of_vector;
  vector_of_vector.reserve(headers.size());
  {
    vector_of_vector.push_back(&id_vec);
    vector_of_vector.push_back(&parser_duration_);
    vector_of_vector.push_back(&cpp_ast_size_);
    vector_of_vector.push_back(&transform_duration_);
    vector_of_vector.push_back(&active_cpp_ast_size1_);
    vector_of_vector.push_back(&analysis_duration_);
    vector_of_vector.push_back(&active_cpp_ast_size2_);
    vector_of_vector.push_back(&conversion_duration_);
    vector_of_vector.push_back(&ir_nodes_);
  }

  if (print_vertical) {
    // Print vertically.
    auto vec2line = [=](const VectorPtrType &vec) -> std::string {
      return absl::visit(
          Visitor{[=](std::vector<absl::Duration> *vec_d) -> std::string {
                    return absl::StrJoin(
                        *vec_d, ",", [=](std::string *out, absl::Duration d) {
                          absl::StrAppend(out, absl::FormatDuration(d));
                        });
                  },
                  [=](std::vector<int64_t> *vec_num) -> std::string {
                    return absl::StrJoin(
                        *vec_num, ",", [=](std::string *out, int64_t num) {
                          absl::StrAppend(out, std::to_string(num));
                        });
                  }},
          vec);
    };

    for (int idx = 0; idx < headers.size(); ++idx) {
      const VectorPtrType &vec = vector_of_vector[idx];
      resutl += absl::StrFormat("%s,%s\n", headers[idx], vec2line(vec, ","));
    }
  } else {
    // Print horizontally.
    result += absl::StrFormat("%s\n", absl::StrJoin(headers, ","));

    for (int row = 0; row < num_sample_; ++row) {
      // Deal withn `vector_of_vector[:][row]`.
      for (int col = 0; col < headers.size(); ++col) {
        // Deal with `vector_of_vector[col][row]`.
        const VectorPtrType &colonm = vector_of_vector[col];

        if (std::holds_alternative<std::vector<absl::Duration> *>(colonm)) {
          auto colonm_d_ptr = std::get<std::vector<absl::Duration> *>(colonm);

          result += absl::FormatDuration(colonm_d_ptr->at(row));
        } else {
          auto colonm_num_ptr = std::get<std::vector<absl::int64_t> *>(colonm);

          result += std::to_string(colonm_num_ptr->at(row));
        }

        result += (col + 1 == headers.size()) ? "\n" : ",";
      }
    }
  }

  return result;
}
} // namespace xls::p5