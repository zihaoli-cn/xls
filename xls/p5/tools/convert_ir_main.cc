
// What does this executable do?
// 1. load P5 AST json file
// 2. parse AST from json
// 3. visualize the loaded AST in text format

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "xls/common/init_xls.h"
#include "xls/common/logging/log_flags.h"
#include "xls/common/logging/logging.h"
#include "xls/common/logging/vlog_is_on.h"
#include "xls/common/status/status_macros.h"
#include "xls/ir/package.h"
#include "xls/p5/ir_converter.h"
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
Converts a P5 AST json input file to XLS IR.

Successfully converted XLS IR is printed to stdout; errors are printed to
stderr.

Example invocation for a particular function:

  convert_ir_main path/to/example.json
)";
}

absl::Status RealMain(std::string_view filename) {
  XLS_ASSIGN_OR_RETURN(std::unique_ptr<nlohmann::json> json,
                       p5::LoadJson(filename));
  XLS_ASSIGN_OR_RETURN(std::unique_ptr<p5::Module> module,
                       p5::ParseModuleFromJson(*json, nullptr));
  XLS_ASSIGN_OR_RETURN(std::unique_ptr<Package> package,
                       p5::ConvertP5AstModuleToPackage(module.get(), nullptr));
  std::cout << package->DumpIr();
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