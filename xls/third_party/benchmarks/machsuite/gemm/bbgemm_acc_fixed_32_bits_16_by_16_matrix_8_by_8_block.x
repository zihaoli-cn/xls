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

import xls.third_party.benchmarks.machsuite.gemm.gemm

// Instantiation of bbgemm_acc_fixed template.
// Operates of 16x16 matrices 32-bits numbers.
// 8x8 tiling is used.
fn bbgemm_acc_fixed_32_bits_16_by_16_matrix_8_by_8_block(
  m1: s32[256], m2: s32[256]) -> s32[256] {
  gemm::bbgemm_acc_fixed<u32:32, u32:16, u32:8>(m1, m2)
}
