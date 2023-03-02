#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "xls/common/init_xls.h"
#include "xls/common/logging/log_flags.h"
#include "xls/common/logging/logging.h"
#include "xls/common/logging/vlog_is_on.h"
#include "xls/common/status/status_macros.h"
#include "xls/p5/json_ast_parser.h"
#include "xls/p5/util/json.hpp"
#include "xls/p5/util/load_json.h"
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
using json = nlohmann::json;
using namespace std;

namespace xls {
namespace {
const char *kUsage = R"(
Round trip: parse and dump json AST.

Example invocation for a particular function:

  json_ast_round_trip_main path/to/p5ast.json
)";
}

absl::Status RealMain(std::string_view filename) {
  XLS_ASSIGN_OR_RETURN(std::unique_ptr<nlohmann::json> json,
                       p5::LoadJson(filename));
  XLS_ASSIGN_OR_RETURN(std::unique_ptr<p5::Module> module,
                       p5::ParseModuleFromJson(*json, nullptr));

  std::cout << module->ToJson().dump(4);
  return absl::OkStatus();
}

} // namespace xls
int main(int argc, char *argv[]) {
  std::vector<absl::string_view> args = xls::InitXls(xls::kUsage, argc, argv);
  if (args.size() > 1) {
    XLS_LOG(QFATAL) << "Wrong number of command-line arguments; got more than "
                       "one filenames";
  }

  absl::SetFlag(&FLAGS_alsologtostderr, true);
  // absl::SetFlag(&FLAGS_minloglevel, 0);
  // absl::SetFlag(&FLAGS_v, 4);

  if (args.empty()) {
    XLS_LOG(QFATAL)
        << "Wrong number of command-line arguments; no input file specified";
  }

  std::string filename = std::string(args[0]);
  XLS_QCHECK_OK(xls::RealMain(filename));
  return 0;
}