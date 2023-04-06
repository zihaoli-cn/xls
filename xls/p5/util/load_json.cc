#include "xls/p5/util/load_json.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "xls/common/logging/logging.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
namespace xls::p5 {

namespace {
absl::StatusOr<std::unique_ptr<nlohmann::json>>
LoadJsonWithoutProfile(std::string_view filename) {
  std::ifstream ifile;
  ifile.open(std::string(filename), std::ios_base::in);
  if (!ifile.is_open()) {
    XLS_LOG(WARNING) << "Fail to load " << filename << ": file doesn't exist.";
    return absl::NotFoundError(absl::StrCat(filename, " is missing"));
  }

  std::unique_ptr<nlohmann::json> result = std::make_unique<nlohmann::json>();
  try {
    ifile >> *result; // load json from ifstream
  } catch (const nlohmann::detail::parse_error &e) {
    // parse error
    XLS_LOG(WARNING) << "Fail to parse json from" << filename;
    return absl::InvalidArgumentError(
        absl::StrCat("the loaded file ", filename,
                     " is not a regular json file: ", e.what()));
  }
  return std::move(result);
}

absl::StatusOr<std::unique_ptr<nlohmann::json>>
LoadJsonWithProfile(std::string_view filename, JsonProfiler *profiler) {
  // Open file.
  std::ifstream ifile;
  ifile.open(std::string(filename), std::ios_base::in);
  if (!ifile.is_open()) {
    XLS_LOG(WARNING) << "Fail to load " << filename << ": file doesn't exist.";
    return absl::NotFoundError(absl::StrCat(filename, " is missing"));
  }

  // Read file into string.
  std::string file_content((std::istreambuf_iterator<char>(ifile)),
                           (std::istreambuf_iterator<char>()));

  // Define callback function for profiler.
  nlohmann::json::parser_callback_t cb =
      [&](int depth, nlohmann::detail::parse_event_t event,
          nlohmann::json &parsed) -> bool {
    if (event == nlohmann::detail::parse_event_t::object_start) {
      ++profiler->object;
    } else if (event == nlohmann::detail::parse_event_t::key) {
      ++profiler->key;
    } else if (event == nlohmann::detail::parse_event_t::value) {
      ++profiler->value;
    } else if (event == nlohmann::detail::parse_event_t::array_start) {
      ++profiler->array;
    }
    profiler->max_depth = std::max(profiler->max_depth, (int64_t)depth);
    profiler->depth_series.push_back(depth);

    // The parsed json should be kept.
    return true;
  };

  std::unique_ptr<nlohmann::json> result = std::make_unique<nlohmann::json>();
  try {
    *result = std::move(nlohmann::json::parse(file_content, cb));
  } catch (const nlohmann::detail::parse_error &e) {
    // parse error
    XLS_LOG(WARNING) << "Fail to parse json from" << filename;
    return absl::InvalidArgumentError(
        absl::StrCat("the loaded file ", filename,
                     " is not a regular json file: ", e.what()));
  }
  return std::move(result);
}

} // namespace

absl::StatusOr<std::unique_ptr<nlohmann::json>>
LoadJson(std::string_view filename, JsonProfiler *profiler) {
  if (profiler) {
    return std::move(LoadJsonWithProfile(filename, profiler));
  } else {
    return std::move(LoadJsonWithoutProfile(filename));
  }
}

} // namespace xls::p5