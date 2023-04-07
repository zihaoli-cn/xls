
#pragma once

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"

#include "xls/p5/util/json.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <vector>
namespace xls::p5 {

struct JsonProfiler {
  JsonProfiler();

  int64_t object;
  int64_t key;
  int64_t value;
  int64_t array;
  int64_t total;
  int64_t max_depth;

  int64_t expr_count;
  int64_t stmt_count;
  int64_t meta_count;
  int64_t ident_count;

  absl::flat_hash_set<std::string> expr_tyname;
  absl::flat_hash_set<std::string> stmt_tyname;

  std::vector<int64_t> depth_series;
};

// try Load json from the give filename
// Args:
//   filename: the filename of the json file
// Returns:
//   json object, using nlohmann's json library
absl::StatusOr<std::unique_ptr<nlohmann::json>>
LoadJson(std::string_view filename, JsonProfiler *profiler = nullptr);

} // namespace xls::p5
