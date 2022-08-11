// Copyright 2020 The XLS Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Takes in an IR file and produces an IR file that has been run through the
// standard optimization pipeline.

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "xls/common/file/filesystem.h"
#include "xls/common/init_xls.h"
#include "xls/common/logging/logging.h"
#include "xls/common/status/status_macros.h"
#include "xls/ir/ir_parser.h"
#include "xls/ir/package.h"

const char kUsage[] = R"(
Print node breakdown.

Expected invocation:
  print_nodes_break_down_main <IR file>
where:
  - <IR file> is the path to the input IR file. '-' denotes stdin as input.

Example invocation:
  print_nodes_break_down_main path/to/file.ir
)";


namespace xls::tools {
namespace {


void PrintNodeBreakdown(Package* p) {
  for(const auto& f : p->functions()) {
    std::cerr << absl::StreamFormat("Entry function (%s) node count: %d nodes\n",
                                    f->name(), f->node_count());
    std::vector<Op> ops;
    absl::flat_hash_map<Op, int64_t> op_count;
    for (Node* node : f->nodes()) {
      if (!op_count.contains(node->op())) {
        ops.push_back(node->op());
      }
      op_count[node->op()] += 1;
    }
    std::sort(ops.begin(), ops.end(),
              [&](Op a, Op b) { return op_count.at(a) > op_count.at(b); });
    std::cerr << "Breakdown by op of all nodes in the graph:" << std::endl;
    for (Op op : ops) {
      std::cerr << absl::StreamFormat("  %15s : %5d (%5.2f%%)\n", OpToString(op),
                                      op_count.at(op),
                                      100.0 * op_count.at(op) / f->node_count());
    }
  }
}


absl::Status RealMain(absl::string_view input_path) {
  if (input_path == "-") {
    input_path = "/dev/stdin";
  }
  XLS_ASSIGN_OR_RETURN(std::string ir, GetFileContents(input_path));
  XLS_ASSIGN_OR_RETURN(std::unique_ptr<Package> package,
                       Parser::ParsePackage(ir, input_path));
  PrintNodeBreakdown(package.get());
  return absl::OkStatus();
}

}  // namespace
}  // namespace xls::tools

int main(int argc, char **argv) {
  std::vector<absl::string_view> positional_arguments =
      xls::InitXls(kUsage, argc, argv);

  if (positional_arguments.empty()) {
    XLS_LOG(QFATAL) << absl::StreamFormat("Expected invocation: %s <path>",
                                          argv[0]);
  }

  XLS_QCHECK_OK(xls::tools::RealMain(positional_arguments[0]));
  return EXIT_SUCCESS;
}
