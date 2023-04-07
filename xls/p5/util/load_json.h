
#pragma once

#include "absl/status/statusor.h"
#include "xls/p5/util/json.hpp"
#include <memory>
#include <string_view>
#include <vector>
namespace xls::p5 {

struct JsonProfiler {
  JsonProfiler() {
    object = 0;
    key = 0;
    value = 0;
    array = 0;
    total = 0;
    max_depth = 0;
  }

  int64_t object;
  int64_t key;
  int64_t value;
  int64_t array;
  int64_t total;
  int64_t max_depth;
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
