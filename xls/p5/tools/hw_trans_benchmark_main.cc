#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "xls/common/file/filesystem.h"
#include "xls/common/init_xls.h"
#include "xls/common/logging/log_flags.h"
#include "xls/common/logging/logging.h"
#include "xls/common/logging/vlog_is_on.h"
#include "xls/common/status/status_macros.h"
#include "xls/p5/benchmark.h"

ABSL_FLAG(std::string, benchmark_dir, "xls/p5/data/benchmark",
          "json ast benchmark's directory ");

ABSL_FLAG(bool, print_vertical, true, "print benchmark's result vertically");

ABSL_FLAG(std::string, prefix, "bench", "P5 Json file's name prefix");

ABSL_FLAG(std::string, json_depth_prefix, "xls/p5/data/json_depth/series",
          "P5 Json depth prefix file's preifx");

namespace xls {
namespace {
const char *kUsage = R"(
Run benchmark for the hardware generation process for P5 AST.

Example invocation for a particular function:

  hw_trans_benchmark_main 
)";
}

absl::Status RealMain(const std::string &benchmark_dir,
                      const std::string &prefix, bool print_vertical,
                      const std::string &json_depth_prefix) {
  XLS_ASSIGN_OR_RETURN(int32_t size,
                       p5::CheckBenchmarkSize(benchmark_dir, prefix));

  std::cerr << size << std::endl;

  p5::TranslationBenchmark benchmark(benchmark_dir, prefix, size);
  XLS_RETURN_IF_ERROR(benchmark.Run());

  std::vector<size_t> idx_vec = {2, 3, 4};
  // benchmark.SaveJsonDepthSeries(idx_vec, json_depth_prefix);

  // std::cout << benchmark.DumpJsonStatistics(print_vertical) << std::endl;

  // std::cout << benchmark.DumpJsonTynameCounters(print_vertical) << std::endl;

  // std::cout << benchmark.DumpHWTransTime(print_vertical) << std::endl;

  // std::cout << benchmark.DumpAstNodesRealStaffTime(print_vertical) <<
  // std::endl;

  std::cout << benchmark.DumpBitCountOutDegree(print_vertical) << std::endl;

  // std::cout << "\n" << benchmark.DumpDataDepResult(print_vertical) <<
  // std::endl;

  return absl::OkStatus();
}

}  // namespace xls

int main(int argc, char **argv) {
  std::vector<absl::string_view> args = xls::InitXls(xls::kUsage, argc, argv);

  std::string benchmark_dir = absl::GetFlag(FLAGS_benchmark_dir);
  std::string prefix = absl::GetFlag(FLAGS_prefix);
  bool print_vertical = absl::GetFlag(FLAGS_print_vertical);
  std::string json_depth_prefix = absl::GetFlag(FLAGS_json_depth_prefix);

  XLS_QCHECK_OK(
      xls::RealMain(benchmark_dir, prefix, print_vertical, json_depth_prefix));
  return EXIT_SUCCESS;
}
