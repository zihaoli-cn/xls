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

// Random-sampling test for the 64-bit FMA (fused multiply-add) module.
#include <cmath>

#include "absl/flags/flag.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "xls/common/init_xls.h"
#include "xls/common/logging/logging.h"
#include "xls/common/math_util.h"
#include "xls/modules/fma_64_jit_wrapper.h"
#include "xls/tools/testbench.h"

ABSL_FLAG(int, num_threads, 0,
          "Number of threads to use. Set to 0 to use all available.");
ABSL_FLAG(int64_t, num_samples, 1024 * 1024,
          "Number of random samples to test.");

namespace xls {

using Float3x64 = std::tuple<double, double, double>;

uint64_t fp_sign(uint64_t f) { return f >> 63; }

uint64_t fp_exp(uint64_t f) { return (f >> 52) & 0x7ff; }

uint64_t fp_sfd(uint64_t f) { return f & 0xfffffffffffffull; }

Float3x64 IndexToInput(uint64_t index) {
  // Skip subnormal inputs - we currently don't handle them.
  thread_local absl::BitGen bitgen;
  auto generate_non_subnormal = []() {
    uint64_t value = 0;
    while (fp_exp(value) == 0) {
      value = absl::Uniform(bitgen, 0u, std::numeric_limits<uint64_t>::max());
    }
    return value;
  };

  uint64_t a = generate_non_subnormal();
  uint64_t b = generate_non_subnormal();
  uint64_t c = generate_non_subnormal();
  return Float3x64(absl::bit_cast<double>(a), absl::bit_cast<double>(b),
                   absl::bit_cast<double>(c));
}

double ComputeExpected(Float3x64 input) {
  return fma(std::get<0>(input), std::get<1>(input), std::get<2>(input));
}

double ComputeActual(Fma64* jit_wrapper, Float3x64 input) {
  double result =
      jit_wrapper
          ->Run(std::get<0>(input), std::get<1>(input), std::get<2>(input))
          .value();
  return result;
}

bool CompareResults(double a, double b) {
  return a == b || (std::isnan(a) && std::isnan(b)) ||
         (ZeroOrSubnormal(a) && ZeroOrSubnormal(b));
}

void LogMismatch(uint64_t index, Float3x64 input, double expected,
                 double actual) {
  uint64_t a_int = absl::bit_cast<uint64_t>(std::get<0>(input));
  uint64_t b_int = absl::bit_cast<uint64_t>(std::get<1>(input));
  uint64_t c_int = absl::bit_cast<uint64_t>(std::get<2>(input));
  uint64_t exp_int = absl::bit_cast<uint64_t>(expected);
  uint64_t act_int = absl::bit_cast<uint64_t>(actual);
  XLS_LOG(ERROR) << absl::StrFormat(
      "Value mismatch at index %d, input: \n"
      "  A       : 0x%016x (0x%01x, 0x%03x, 0x%013x)\n"
      "  B       : 0x%016x (0x%01x, 0x%03x, 0x%013x)\n"
      "  C       : 0x%016x (0x%01x, 0x%03x, 0x%013x)\n"
      "  Expected: 0x%016x (0x%01x, 0x%03x, 0x%013x)\n"
      "  Actual  : 0x%016x (0x%01x, 0x%03x, 0x%013x)",
      index, a_int, fp_sign(a_int), fp_exp(a_int), fp_sfd(a_int), b_int,
      fp_sign(b_int), fp_exp(b_int), fp_sfd(b_int), c_int, fp_sign(c_int),
      fp_exp(c_int), fp_sfd(c_int), exp_int, fp_sign(exp_int), fp_exp(exp_int),
      fp_sfd(exp_int), act_int, fp_sign(act_int), fp_exp(act_int),
      fp_sfd(act_int));
}

absl::Status RealMain(int64_t num_samples, int num_threads) {
  Testbench<Fma64, Float3x64, double> testbench(
      0, num_samples, /*max_failures=*/1, IndexToInput, ComputeExpected,
      ComputeActual, CompareResults, LogMismatch);
  if (num_threads != 0) {
    XLS_RETURN_IF_ERROR(testbench.SetNumThreads(num_threads));
  }
  return testbench.Run();
}

}  // namespace xls

int main(int argc, char** argv) {
  xls::InitXls(argv[0], argc, argv);
  XLS_QCHECK_OK(xls::RealMain(absl::GetFlag(FLAGS_num_samples),
                              absl::GetFlag(FLAGS_num_threads)));
}