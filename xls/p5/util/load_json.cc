#include "xls/p5/util/load_json.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "xls/common/logging/logging.h"
#include <fstream>
#include <string>
namespace xls::p5 {

absl::StatusOr<std::unique_ptr<nlohmann::json>>
LoadJson(std::string_view filename) {
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

} // namespace xls::p5