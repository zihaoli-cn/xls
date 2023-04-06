#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "xls/common/init_xls.h"
#include "xls/common/logging/log_flags.h"
#include "xls/common/logging/logging.h"
#include "xls/common/logging/vlog_is_on.h"
#include "xls/common/status/status_macros.h"
#include "xls/data_structures/union_find.h"

#include "xls/p5/ast_mutation.h"
#include "xls/p5/json_ast_parser.h"
#include "xls/p5/util/json.hpp"
#include "xls/p5/util/load_json.h"
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
using json = nlohmann::json;
using namespace std;

ABSL_FLAG(std::string, prefix, "out", "The output json's prefix");

ABSL_FLAG(int64_t, samples, 10, "The number of generated samples");

ABSL_FLAG(int64_t, precision, 1000,
          "The minimal granularity of probability. e.g. 1/1000.");

ABSL_FLAG(int64_t, min_stmt, 5, "The minimal number of stmts within the AST.");

namespace xls {
namespace {
const char *kUsage = R"(
Example invocation for a particular function:

  ast_gen_mod_main path/to/p5ast.json
)";
}

std::vector<int> GenerateRandInts(int num, int precision, int max, int min) {
  XLS_CHECK(num >= 1 && precision > 1);

  std::vector<int> result;
  result.reserve(num);

  for (int i = 0; i < num; ++i) {
    int v = 1 + (rand() % precision);

    result.push_back(v < min ? min : (v > max ? max : v));
  }
  return result;
}

// Support all: StmtBlock, If, IfElse, Assign
p5::MutaionOptions GenerateRandomOptions1(int precision, int max_rand_int,
                                          int min_rand_int) {
  std::vector<int> params =
      GenerateRandInts(10, precision, max_rand_int, min_rand_int);
  p5::MutaionOptions options =
      p5::MutaionOptionsBuilder(precision)
          .SupportStmtBlock(params[0], params[1], params[2], params[3],
                            params[4])
          .SupportIf(params[5], params[6])
          .SupportIfElse(params[7], params[8])
          .SupportAssign(params[9]);
  return options;
}

// Support: StmtBlock, If, IfElse
p5::MutaionOptions GenerateRandomOptions2(int precision, int max_rand_int,
                                          int min_rand_int) {
  std::vector<int> params =
      GenerateRandInts(9, precision, max_rand_int, min_rand_int);
  p5::MutaionOptions options =
      p5::MutaionOptionsBuilder(precision)
          .SupportStmtBlock(params[0], params[1], params[2], params[3],
                            params[4])
          .SupportIf(params[5], params[6])
          .SupportIfElse(params[7], params[8]);
  return options;
}

// Support: StmtBlock, Assign
p5::MutaionOptions GenerateRandomOptions3(int precision, int max_rand_int,
                                          int min_rand_int) {
  std::vector<int> params =
      GenerateRandInts(6, precision, max_rand_int, min_rand_int);
  p5::MutaionOptions options =
      p5::MutaionOptionsBuilder(precision)
          .SupportStmtBlock(params[0], params[1], params[2], params[3],
                            params[4])
          .SupportAssign(params[5]);
  return options;
}

// Support: If, IfElse, Assign
p5::MutaionOptions GenerateRandomOptions4(int precision, int max_rand_int,
                                          int min_rand_int) {
  std::vector<int> params =
      GenerateRandInts(5, precision, max_rand_int, min_rand_int);
  p5::MutaionOptions options = p5::MutaionOptionsBuilder(precision)
                                   .SupportIf(params[0], params[1])
                                   .SupportIfElse(params[2], params[3])
                                   .SupportAssign(params[4]);
  return options;
}

absl::StatusOr<nlohmann::json> RandomChangeJsonAst(
    nlohmann::json &json,
    std::function<p5::MutaionOptions(int, int, int)> opt_builder, int precision,
    int max_rand_int, int min_rand_int, int min_stmt) {
  std::unique_ptr<p5::Module> module = nullptr;
  do {
    std::vector<int> params =
        GenerateRandInts(10, precision, max_rand_int, min_rand_int);
    p5::MutaionOptions options =
        opt_builder(precision, max_rand_int, min_rand_int);

    XLS_ASSIGN_OR_RETURN(module, p5::ParseModuleFromJson(json, nullptr));

    p5::AstMutation mut(options);
    mut.VisitModule(module.get());
  } while (module->body->CountActiveStmt() <= min_stmt);

  std::cerr << absl::StrFormat("contains %d stmts",
                               module->body->CountActiveStmt())
            << std::endl;

  return module->ToJson();
}

/*
absl::Status RealMain(std::string_view filename, std::string_view prefix,
                      int samples, int precision, int min_stmt) {
  std::srand(std::time(0));
  std::vector<std::string> generated_json;

  UnionFind<int> uf;

  std::vector<std::function<p5::MutaionOptions(int, int, int)>> opt_builders;
  opt_builders.push_back(GenerateRandomOptions1);
  // opt_builders.push_back(GenerateRandomOptions2);
  // opt_builders.push_back(GenerateRandomOptions3);
  // opt_builders.push_back(GenerateRandomOptions4);

  for (auto opt_builder : opt_builders) {
    for (int i = 0; i < samples; ++i) {
      std::cerr << "iter-" << i << std::endl;
      uf.Insert(i);

      XLS_ASSIGN_OR_RETURN(std::unique_ptr<nlohmann::json> json_ptr,
                           p5::LoadJson(filename));

      XLS_ASSIGN_OR_RETURN(nlohmann::json changed_json,
                           RandomChangeJsonAst(*json_ptr, opt_builder,
                                               precision, precision * 0.6,
                                               precision / 10, min_stmt));
      generated_json.push_back(changed_json.dump());

      json_ptr.release();
    }
  }

  bool changed = true;
  int iter = 0;
  while (changed) {
    changed = false;
    ++iter;

    for (int i = 0; i < samples - 1; ++i) {
      for (int j = i + 1; j < samples; ++j) {
        if (generated_json[i] == generated_json[j]) {
          uf.Union(i, j);
          //
          //XLS_ASSIGN_OR_RETURN(std::unique_ptr<nlohmann::json> json_ptr,
          //                     p5::LoadJson(filename));

          //XLS_ASSIGN_OR_RETURN(nlohmann::json changed_json,
                               //RandomChangeJsonAst(*json_ptr, precision + i));
          //generated_json[j] = changed_json.dump();

          //json_ptr.release();

          //changed = true;
          //
std::cerr << absl::StrFormat("[%d, %d] duplicated", i, j) << std::endl;
}
}
}
}

std::cerr << absl::StrFormat("size = %d", uf.GetRepresentatives().size())
          << std::endl;

int idx = 0;
for (int i : uf.GetRepresentatives()) {
  std::ofstream(absl::StrFormat("%s%d.json", prefix, idx++), std::ios::out)
      << generated_json[i];
}

return absl::OkStatus();
}
*/

absl::Status RealMain(std::string_view filename, std::string_view prefix,
                      int samples, int precision, int min_stmt) {
  std::vector<p5::MutaionOptions> opt_list;
  // opt_list.push_back(
  //     p5::MutaionOptionsBuilder(100).SupportIf(25, 0).SupportIfElse(25, 0));
  // opt_list.push_back(
  //     p5::MutaionOptionsBuilder(100).SupportIf(30, 0).SupportIfElse(10, 70));
  // opt_list.push_back(
  //     p5::MutaionOptionsBuilder(100).SupportIf(10, 0).SupportIfElse(5, 100));
  opt_list.push_back(
      p5::MutaionOptionsBuilder(100).SupportStmtBlock(10, 10, 10, 5, 0));
  opt_list.push_back(
      p5::MutaionOptionsBuilder(100).SupportStmtBlock(15, 15, 15, 5, 5));
  opt_list.push_back(p5::MutaionOptionsBuilder(100)
                         .SupportStmtBlock(10, 10, 10, 5, 0)
                         .SupportAssign(20));
  opt_list.push_back(p5::MutaionOptionsBuilder(100).SupportAssign(40));

  for (int i = 0; i < opt_list.size(); ++i) {
    std::cerr << "iter-" << i << std::endl;

    XLS_ASSIGN_OR_RETURN(std::unique_ptr<nlohmann::json> json,
                         p5::LoadJson(filename));
    XLS_ASSIGN_OR_RETURN(std::unique_ptr<p5::Module> module,
                         p5::ParseModuleFromJson(*json, nullptr));

    p5::AstMutation mut(opt_list.at(i));
    mut.VisitModule(module.get());

    std::ofstream(absl::StrFormat("out%d.json", i), std::ios::out)
        << module->ToJson().dump(4);

    json.release();
    module.release();
  }

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
  absl::SetFlag(&FLAGS_minloglevel, 0);
  absl::SetFlag(&FLAGS_v, 4);

  if (args.empty()) {
    XLS_LOG(QFATAL)
        << "Wrong number of command-line arguments; no input file specified";
  }

  std::string filename = std::string(args[0]);
  XLS_QCHECK_OK(xls::RealMain(
      filename, absl::GetFlag(FLAGS_prefix), absl::GetFlag(FLAGS_samples),
      absl::GetFlag(FLAGS_precision), absl::GetFlag(FLAGS_min_stmt)));
  return 0;
}