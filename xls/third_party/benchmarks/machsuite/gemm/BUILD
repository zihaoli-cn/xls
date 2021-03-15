# Copyright 2021 The XLS Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("//xls/build_rules:build_defs.bzl", "dslx_test")

package(
    default_visibility = ["//xls:xls_internal"],
    licenses = ["notice"],
)

cc_binary(
    name = "gemm_reference",
    srcs = ["gemm_reference.cc"],
    data = ["test_data/a_matrix_64_64",
            "test_data/b_matrix_64_64",
            ],
    deps = [
        "//xls/common/file:filesystem",
        "//xls/common/file:get_runfile_path",
        "//xls/common:init_xls",
        "//xls/common:integral_types",
        "//xls/common/logging",
        "//xls/common/status:status_macros",
        "//xls/third_party/benchmarks/common:data_io",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)

dslx_test(
    name = "gemm",
    srcs = ["gemm.x"],
    deps = ["//xls/modules:fpadd_2x32.x",
            "//xls/modules:fpmul_2x32.x",
            "test_data/dslx_test_data.x"],
    # Ir optimization eats up all the memory and bricks my desktop (16 GB of ram).
    convert_ir = False,
)

dslx_test(
    name = "bbgemm_acc_fixed_32_bits_16_by_16_matrix_8_by_8_block",
    srcs = ["bbgemm_acc_fixed_32_bits_16_by_16_matrix_8_by_8_block.x"],
    deps = ["//xls/modules:fpadd_2x32.x",
            "//xls/modules:fpmul_2x32.x",
            "gemm.x"],
)
