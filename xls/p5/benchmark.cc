#include "xls/p5/benchmark.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/types/variant.h"

#include "xls/common/logging/logging.h"
#include "xls/common/status/status_macros.h"
#include "xls/common/visitor.h"

#include "xls/delay_model/analyze_critical_path.h"
#include "xls/ir/function.h"
#include "xls/p5/ir_converter.h"
#include "xls/p5/json_ast_parser.h"
#include "xls/p5/util/load_json.h"

namespace xls::p5 {

absl::StatusOr<int32_t> CheckBenchmarkSize(const std::string &benchmark_dir,
                                           const std::string &prefix) {
  std::filesystem::path base = benchmark_dir;
  absl::Status status = absl::OkStatus();
  int32_t counter = 0;
  while (status.ok()) {
    std::filesystem::path current = base / prefix;
    current += absl::StrFormat("%d.json", counter++);
    status = FileExists(current);
  }

    return counter - 1;
}

TranslationBenchmark::TranslationBenchmark(const std::string &benchmark_dir,
                                           const std::string &prefix,
                                           int32_t num_sample,
                                           bool profile_json)
    : profile_json_(profile_json), benchmark_dir_(benchmark_dir),
      prefix_(prefix), num_sample_(num_sample), current_idx_(0) {

  json_info_.reserve(num_sample);

  json_ast_.reserve(num_sample);
  cpp_ast_.reserve(num_sample);
  load_json_duration_.reserve(num_sample);
  parser_duration_.reserve(num_sample);

  cpp_ast_size_.reserve(num_sample);

  transform_duration_.reserve(num_sample);

  active_cpp_ast_size1_.reserve(num_sample);

  analysis_duration_.reserve(num_sample);

  active_cpp_ast_size2_.reserve(num_sample);

  conversion_duration_.reserve(num_sample);

  ir_.reserve(num_sample);

  pre_staff_duration_.reserve(num_sample);
  real_staff_duration_.reserve(num_sample);
  total_duration_.reserve(num_sample);

  ir_nodes_.reserve(num_sample);
  ir_edges_.reserve(num_sample);
  ir_cp_nodes_.reserve(num_sample);
  ir_cp_delay_.reserve(num_sample);
  ir_params_.reserve(num_sample);
  ir_params_bitcount_.reserve(num_sample);
}

std::string TranslationBenchmark::GetCurrentFileName() const {
  std::filesystem::path path = benchmark_dir_;
  path /= absl::StrFormat("%s%d.json", prefix_, current_idx_);

  return path.string();
}

absl::Status TranslationBenchmark::Parse() {
  if (profile_json_) {
  json_info_.push_back(JsonProfiler());
  }

  absl::Time load_json_start = absl::Now();

  std::unique_ptr<nlohmann::json> json;
  if (profile_json_) {
    XLS_ASSIGN_OR_RETURN(json,
                       LoadJson(GetCurrentFileName(), &(json_info_.back())));
  } else {
    XLS_ASSIGN_OR_RETURN(json, LoadJson(GetCurrentFileName(), nullptr));
  }

  absl::Duration load_json_duration = absl::Now() - load_json_start;
  absl::Time parser_start = absl::Now();

  XLS_ASSIGN_OR_RETURN(std::unique_ptr<Module> ast_module,
                       ParseModuleFromJson(*json));

  if (current_idx_ != json_ast_.size() || current_idx_ != cpp_ast_.size() ||
      current_idx_ != load_json_duration_.size() ||
      current_idx_ != parser_duration_.size()) {
    return absl::InternalError("Invalid size");
  }

  json_ast_.push_back(std::move(json));
  cpp_ast_.push_back(std::move(ast_module));
  load_json_duration_.push_back(load_json_duration);
  parser_duration_.push_back(absl::Now() - parser_start);

  return absl::OkStatus();
}

absl::Status TranslationBenchmark::Translate() {
  IrConverterProfiler profiler;
  XLS_ASSIGN_OR_RETURN(
      std::unique_ptr<Package> ir_package,
      ConvertP5AstModuleToPackage(cpp_ast_.back().get(), nullptr, &profiler));

  if (current_idx_ != cpp_ast_size_.size() ||
      current_idx_ != transform_duration_.size() ||
      current_idx_ != active_cpp_ast_size1_.size() ||
      current_idx_ != analysis_duration_.size() ||
      current_idx_ != active_cpp_ast_size2_.size() ||
      current_idx_ != conversion_duration_.size() ||
      current_idx_ != ir_.size() ||
      current_idx_ != pre_staff_duration_.size() ||
      current_idx_ != real_staff_duration_.size() ||
      current_idx_ != total_duration_.size()) {
    return absl::InternalError("Invalid size");
  }

  cpp_ast_size_.push_back(profiler.active_ast_num1);
  transform_duration_.push_back(profiler.tranform_duration);
  active_cpp_ast_size1_.push_back(profiler.active_ast_num2);
  analysis_duration_.push_back(profiler.analysis_duration);
  active_cpp_ast_size2_.push_back(profiler.active_ast_num3);
  conversion_duration_.push_back(profiler.conversion_duration);
  pre_staff_duration_.push_back(load_json_duration_.back() +
                                parser_duration_.back());
  real_staff_duration_.push_back(profiler.tranform_duration +
                                 profiler.analysis_duration +
                                 profiler.conversion_duration);
  total_duration_.push_back(
      load_json_duration_.back() + parser_duration_.back() +
      profiler.tranform_duration + profiler.analysis_duration +
      profiler.conversion_duration);
  ir_.push_back(std::move(ir_package));

  return absl::OkStatus();
}

absl::Status TranslationBenchmark::Run() {
  for (int i = 0; i < num_sample_; ++i) {
    XLS_RETURN_IF_ERROR(Parse());
    XLS_RETURN_IF_ERROR(Translate());
    ++current_idx_;
  }
  XLS_RETURN_IF_ERROR(AnalyzeDataDep());

  return absl::OkStatus();
}

  using VectorPtrType =
      absl::variant<std::vector<absl::Duration> *, std::vector<int64_t> *>;
namespace {
std::string DumpTable(size_t num_sample,
                      const std::vector<std::string> &headers,
                      const std::vector<VectorPtrType> &vector_of_vector,
                      bool print_vertical, std::string elem_sep,
                      std::string line_sep) {
  std::string result;
  XLS_CHECK(vector_of_vector.size() == headers.size());

  if (print_vertical) {
    // Print vertically.
    auto vec2line = [=](const VectorPtrType &vec) -> std::string {
      return absl::visit(
          Visitor{[=](std::vector<absl::Duration> *vec_d) -> std::string {
                    return absl::StrJoin(
                        *vec_d, elem_sep,
                        [=](std::string *out, absl::Duration d) {
                          absl::StrAppend(
                              out,
                              absl::StrFormat("\"%s\"",
                                              absl::FormatDuration(absl::Trunc(
                                                  d, absl::Microseconds(10)))));
                        });
                  },
                  [=](std::vector<int64_t> *vec_num) -> std::string {
                    return absl::StrJoin(
                        *vec_num, elem_sep, [=](std::string *out, int64_t num) {
                          absl::StrAppend(out, std::to_string(num));
                        });
                  }},
          vec);
    };

    for (int idx = 0; idx < headers.size(); ++idx) {
      const VectorPtrType &vec = vector_of_vector[idx];
      result +=
          absl::StrFormat("[%s,%s%s],", headers[idx], vec2line(vec), line_sep);
    }
  } else {
    // Print horizontally.
    result +=
        absl::StrFormat("%s%s", absl::StrJoin(headers, elem_sep), line_sep);

    for (int row = 0; row < num_sample; ++row) {
      // Deal withn `vector_of_vector[:][row]`.
      for (int col = 0; col < headers.size(); ++col) {
        // Deal with `vector_of_vector[col][row]`.
        const VectorPtrType &colonm = vector_of_vector[col];

        if (absl::holds_alternative<std::vector<absl::Duration> *>(colonm)) {
          auto colonm_d_ptr = absl::get<std::vector<absl::Duration> *>(colonm);

          result += absl::StrFormat(
              "\"%s\"", absl::FormatDuration(absl::Trunc(
                            colonm_d_ptr->at(row), absl::Microseconds(10))));
        } else {
          auto colonm_num_ptr = absl::get<std::vector<int64_t> *>(colonm);

          result += std::to_string(colonm_num_ptr->at(row));
        }

        result += (col + 1 == headers.size()) ? line_sep : elem_sep;
      }
    }
  }

  return result;
}
} // namespace

std::string TranslationBenchmark::DumpHWTransTime(bool print_vertical) {
  XLS_LOG_IF(WARNING, profile_json_)
      << "contains json callback's time" << std::endl;
  std::string result;

  std::vector<std::string> headers = {
      "\"ID\"",        "\"Json Loader\"", "\"Parser\"",   "\"Transformation\"",
      "\"Analysis\"",  "\"Translation\"", "\"前二模块\"", "\"后三模块\"",
      "\"总运行时间\""};

  std::vector<int64_t> id_vec;
  id_vec.reserve(num_sample_);

  for (int i = 0; i < num_sample_; ++i) {
    id_vec.push_back(i);
  }

  std::vector<VectorPtrType> vector_of_vector;
  vector_of_vector.reserve(headers.size());
  {
    vector_of_vector.push_back(&id_vec);
    vector_of_vector.push_back(&load_json_duration_);
    vector_of_vector.push_back(&parser_duration_);
    vector_of_vector.push_back(&transform_duration_);
    vector_of_vector.push_back(&analysis_duration_);
    vector_of_vector.push_back(&conversion_duration_);
    vector_of_vector.push_back(&pre_staff_duration_);
    vector_of_vector.push_back(&real_staff_duration_);
    vector_of_vector.push_back(&total_duration_);
  }

  return DumpTable(num_sample_, headers, vector_of_vector, print_vertical, ",",
                   "\n");
}

absl::Status TranslationBenchmark::AnalyzeDataDep() {
  XLS_CHECK(packages().size() == num_sample_);
  for (auto &package : packages()) {
    int64_t nodes = 0;
    int64_t edges = 0;
    int64_t params = 0;
    int64_t params_total_bitcount = 0;
    int64_t cp_nodes = 0;
    int64_t cp_delay = 0;
    XLS_CHECK(package->functions().size() == 1);
    for (auto &f : package->functions()) {
      nodes += f->node_count();
      params += f->params().size();

      for (auto param : f->params()) {
        params_total_bitcount += param->BitCountOrDie();
      }

      for (auto node : f->nodes()) {
        edges += node->operand_count();
      }

      XLS_ASSIGN_OR_RETURN(
          std::vector<CriticalPathEntry> cp,
          AnalyzeCriticalPath(f.get(), std::nullopt, TestDelayEstimator()));
      cp_nodes = cp.size();
      cp_delay = cp.front().path_delay_ps;
    }

    ir_nodes_.push_back(nodes);
    ir_edges_.push_back(edges);
    ir_cp_nodes_.push_back(cp_nodes);
    ir_cp_delay_.push_back(cp_delay);
    ir_params_.push_back(params);
    ir_params_bitcount_.push_back(params_total_bitcount);
  }
  return absl::OkStatus();
}

std::string TranslationBenchmark::DumpDataDepResult(bool print_vertical) {
  std::string result;

  std::vector<std::string> headers = {"\"ID\"",
                                      "\"数据依赖图节点数\"",
                                      "\"数据依赖图有向边数\"",
                                      "\"关键路径上节点数\"",
                                      "\"关键路径上总时延\"",
                                      "\"参数数量\"",
                                      "\"参数总位宽\""};

  std::vector<int64_t> id_vec;
  id_vec.reserve(num_sample_);

  for (int i = 0; i < num_sample_; ++i) {
    id_vec.push_back(i);
  }

  std::vector<VectorPtrType> vector_of_vector;
  vector_of_vector.reserve(headers.size());
  {
    vector_of_vector.push_back(&id_vec);
    vector_of_vector.push_back(&ir_nodes_);
    vector_of_vector.push_back(&ir_edges_);
    vector_of_vector.push_back(&ir_cp_nodes_);
    vector_of_vector.push_back(&ir_cp_delay_);
    vector_of_vector.push_back(&ir_params_);
    vector_of_vector.push_back(&ir_params_bitcount_);
  }

  return DumpTable(num_sample_, headers, vector_of_vector, print_vertical, ",",
                   "\n");
}

std::string TranslationBenchmark::DumpJsonStatistics(bool print_vertical) {
  if (!profile_json_) {
    return "";
  }
  std::string result;

  std::vector<std::string> headers = {"\"ID\"",       "\"Object\"", "\"Key\"",
                                      "\"Value\"",    "\"Array\"",  "\"Total\"",
                                      "\"Max Depth\""};

  std::vector<int64_t> id_vec;

  std::vector<int64_t> object_vec;
  std::vector<int64_t> key_vec;
  std::vector<int64_t> val_vec;
  std::vector<int64_t> arr_vec;
  std::vector<int64_t> total_vec;
  std::vector<int64_t> depth_vec;

  id_vec.reserve(num_sample_);

  object_vec.reserve(num_sample_);
  key_vec.reserve(num_sample_);
  val_vec.reserve(num_sample_);
  arr_vec.reserve(num_sample_);
  total_vec.reserve(num_sample_);
  depth_vec.reserve(num_sample_);

  for (int i = 0; i < num_sample_; ++i) {
    id_vec.push_back(i);

    object_vec.push_back(json_info_.at(i).object);
    key_vec.push_back(json_info_.at(i).key);
    val_vec.push_back(json_info_.at(i).value);
    arr_vec.push_back(json_info_.at(i).array);
    total_vec.push_back(json_info_.at(i).total);
    depth_vec.push_back(json_info_.at(i).max_depth);
  }

  std::vector<VectorPtrType> vector_of_vector;
  vector_of_vector.reserve(headers.size());
  {
    vector_of_vector.push_back(&id_vec);

    vector_of_vector.push_back(&object_vec);
    vector_of_vector.push_back(&key_vec);
    vector_of_vector.push_back(&val_vec);
    vector_of_vector.push_back(&arr_vec);
    vector_of_vector.push_back(&total_vec);
    vector_of_vector.push_back(&depth_vec);
  }

  return DumpTable(num_sample_, headers, vector_of_vector, print_vertical, ",",
                   "\n");
}

std::string TranslationBenchmark::DumpJsonTynameCounters(bool print_vertical) {
  if (!profile_json_) {
    return "";
  }
  std::string result;

  std::vector<std::string> headers = {"ID", "Expression", "Statement",
                                      "Identifier", "Other"};

  std::vector<int64_t> id_vec;

  std::vector<int64_t> expr_count;
  std::vector<int64_t> stmt_count;
  std::vector<int64_t> ident_count;
  std::vector<int64_t> meta_count;

  id_vec.reserve(num_sample_);

  expr_count.reserve(num_sample_);
  stmt_count.reserve(num_sample_);
  ident_count.reserve(num_sample_);
  meta_count.reserve(num_sample_);

  for (int i = 0; i < num_sample_; ++i) {
    id_vec.push_back(i);

    expr_count.push_back(json_info_.at(i).expr_count);
    stmt_count.push_back(json_info_.at(i).stmt_count);
    ident_count.push_back(json_info_.at(i).ident_count);
    meta_count.push_back(json_info_.at(i).meta_count);
  }

  std::vector<VectorPtrType> vector_of_vector;
  vector_of_vector.reserve(headers.size());
  {
    vector_of_vector.push_back(&id_vec);

    vector_of_vector.push_back(&expr_count);
    vector_of_vector.push_back(&stmt_count);
    vector_of_vector.push_back(&ident_count);
    vector_of_vector.push_back(&meta_count);
  }

  return DumpTable(num_sample_, headers, vector_of_vector, print_vertical,
                   " & ", "\\\\\n");
}

absl::Status TranslationBenchmark::SaveJsonDepthSeries(
    const std::vector<size_t> &idx_vec,
    const std::filesystem::path &output_file_prefix) {
  XLS_CHECK(profile_json_);

  std::string content;
  for (auto i : idx_vec) {
    XLS_CHECK(i < json_info_.size());
    std::filesystem::path path = output_file_prefix;
    path += absl::StrFormat("%llu.txt", i);

    auto &profiler = json_info_.at(i);

    content.clear();
    content = absl::StrJoin(profiler.depth_series, ",",
                            [=](std::string *out, int64_t v) {
                              return absl::StrAppend(out, std::to_string(v));
                            });

    XLS_RETURN_IF_ERROR(SetFileContents(path, content));
  }
  return absl::OkStatus();
}
} // namespace xls::p5