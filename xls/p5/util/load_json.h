
#pragma once

#include "absl/status/statusor.h"
#include "xls/p5/util/json.hpp"
#include <memory>
#include <string_view>
namespace xls::p5 {

// try Load json from the give filename
// Args:
//   filename: the filename of the json file
// Returns:
//   json object, using nlohmann's json library
absl::StatusOr<std::unique_ptr<nlohmann::json>>
LoadJson(std::string_view filename);

} // namespace xls::p5
