// Copyright 2021 The XLS Authors
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

#include "xls/common/status/status_macros.h"

#include "xls/common/init_xls.h"
#include "xls/common/logging/logging.h"
#include "xls/common/status/status_macros.h"
#include "xls/third_party/benchmarks/common/data_io.h"

const char* kUsage = R"(
Tool for reducing IR to a minimal test case based on an external test.
)";

namespace xls {
namespace {

absl::Status RealMain(absl::string_view data_path) {
  std::vector<float> data;
  XLS_RETURN_IF_ERROR((read_float_data(data_path, &data, /*get_runfile_path=*/false)));
  DisplayFloatData(data, "data", /*display_hex=*/true);

  return absl::OkStatus();
}

}  // namespace
}  // namespace xls

int main(int argc, char** argv) {
  std::vector<absl::string_view> positional_arguments =
      xls::InitXls(kUsage, argc, argv);

  if (positional_arguments.size() != 1 || positional_arguments[0].empty()) {
    XLS_LOG(QFATAL) << "Expected path argument with float data: " << argv[0]
                    << " <ir_path>";
  }

  XLS_QCHECK_OK(xls::RealMain(positional_arguments[0]));
  return 0;
}
