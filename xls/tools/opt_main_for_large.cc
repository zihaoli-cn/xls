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
#include "xls/passes/passes.h"
#include "xls/passes/standard_pipeline.h"

const char kUsage[] = R"(
Takes in an IR file and produces an IR file that has been run through the
standard optimization pipeline.

Successfully optimized IR is printed to stdout.

Expected invocation:
  opt_main <IR file>
where:
  - <IR file> is the path to the input IR file. '-' denotes stdin as input.

Example invocation:
  opt_main path/to/file.ir
)";

// LINT.IfChange
ABSL_FLAG(std::string, top, "", "Top entity to optimize.");
ABSL_FLAG(std::string, ir_dump_path, "",
          "Dump all intermediate IR files to the given directory");
ABSL_FLAG(std::vector<std::string>, run_only_passes, {},
          "If specified, only passes in this comma-separated list of (short) "
          "pass names are be run.");
ABSL_FLAG(std::vector<std::string>, skip_passes, {},
          "If specified, passes in this comma-separated list of (short) "
          "pass names are skipped. If both --run_only_passes and --skip_passes "
          "are specified only passes which are present in --run_only_passes "
          "and not present in --skip_passes will be run.");
ABSL_FLAG(int64_t, convert_array_index_to_select, -1,
          "If specified, convert array indexes with fewer than or "
          "equal to the given number of possible indices (by range analysis) "
          "into chains of selects. Otherwise, this optimization is skipped, "
          "since it can sometimes reduce output quality.");
ABSL_FLAG(int64_t, opt_level, xls::kMaxOptLevel,
          absl::StrFormat("Optimization level. Ranges from 1 to %d.",
                          xls::kMaxOptLevel));
ABSL_FLAG(bool, inline_procs, false,
          "Whether to inline all procs by calling the proc inlining pass. ");
// LINT.ThenChange(//xls/build_rules/xls_ir_rules.bzl)

namespace xls::tools {
namespace {

struct OptOptions {
  int64_t opt_level = xls::kMaxOptLevel;
  absl::string_view entry;
  std::string ir_dump_path = "";
  std::optional<std::string> ir_path = absl::nullopt;
  std::optional<std::vector<std::string>> run_only_passes = absl::nullopt;
  std::vector<std::string> skip_passes;
  std::optional<int64_t> convert_array_index_to_select = std::nullopt;
  bool inline_procs;
};

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


absl::StatusOr<std::string> OptimizeIrForEntry(absl::string_view ir,
                                               const OptOptions& options) {
  if (!options.entry.empty()) {
    XLS_VLOG(3) << "OptimizeIrForEntry; entry: '" << options.entry
                << "'; opt_level: " << options.opt_level;
  } else {
    XLS_VLOG(3) << "OptimizeIrForEntry; opt_level: " << options.opt_level;
  }

  XLS_ASSIGN_OR_RETURN(std::unique_ptr<Package> package,
                       Parser::ParsePackage(ir, options.ir_path));
  if (!options.entry.empty()) {
    XLS_RETURN_IF_ERROR(package->SetTopByName(options.entry));
  }
  std::optional<FunctionBase*> top = package->GetTop();
  if (!top.has_value()) {
    return absl::InternalError(absl::StrFormat(
        "Top entity not set for package: %s.", package->name()));
  }
  XLS_VLOG(3) << "Top entity: '" << top.value()->name() << "'";
  // use custome pipeline
  std::unique_ptr<CompoundPass> pipeline =
      CreateStandardPassPipelineForLargeFile(options.opt_level);
  const PassOptions pass_options = {
      .ir_dump_path = options.ir_dump_path,
      .run_only_passes = options.run_only_passes,
      .skip_passes = options.skip_passes,
      .inline_procs = options.inline_procs,
      .convert_array_index_to_select = options.convert_array_index_to_select,
  };
  PassResults results;
  XLS_RETURN_IF_ERROR(
      pipeline->Run(package.get(), pass_options, &results).status());
  // If opt returns something that obviously can't be codegenned, that's a bug
  // in opt, not codegen.
  //XLS_RETURN_IF_ERROR(xls::VerifyPackage(package.get(), /*codegen=*/true));
  PrintNodeBreakdown(package.get());
  return package->DumpIr();
}

absl::Status RealMain(absl::string_view input_path) {
  if (input_path == "-") {
    input_path = "/dev/stdin";
  }
  XLS_ASSIGN_OR_RETURN(std::string ir, GetFileContents(input_path));
  std::string entry = absl::GetFlag(FLAGS_top);
  std::string ir_dump_path = absl::GetFlag(FLAGS_ir_dump_path);
  std::vector<std::string> run_only_passes =
      absl::GetFlag(FLAGS_run_only_passes);
  int64_t convert_array_index_to_select =
      absl::GetFlag(FLAGS_convert_array_index_to_select);
  const OptOptions options = {
      .opt_level = absl::GetFlag(FLAGS_opt_level),
      .entry = entry,
      .ir_dump_path = ir_dump_path,
      .run_only_passes = run_only_passes.empty()
                             ? absl::nullopt
                             : absl::make_optional(std::move(run_only_passes)),
      .skip_passes = absl::GetFlag(FLAGS_skip_passes),
      .convert_array_index_to_select =
          (convert_array_index_to_select < 0)
              ? std::nullopt
              : std::make_optional(convert_array_index_to_select),
      .inline_procs = absl::GetFlag(FLAGS_inline_procs),
  };
  XLS_ASSIGN_OR_RETURN(std::string opt_ir,
                       tools::OptimizeIrForEntry(ir, options));
  std::cout << opt_ir;
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
