// MachSuite (BSD-3) License
// 
// Copyright (c) 2014-2015, the President and Fellows of Harvard College.
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
// 
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// 
// * Neither the name of Harvard University nor the names of its contributors may
//   be used to endorse or promote products derived from this software without
//   specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <cstdio>
#include <filesystem>
#include <iostream>

#include "absl/flags/flag.h"
#include "absl/strings/str_split.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "xls/common/file/filesystem.h"
#include "xls/common/file/get_runfile_path.h"
#include "xls/common/init_xls.h"
#include "xls/common/logging/logging.h"
#include "xls/common/status/status_macros.h"
#include "xls/third_party/benchmarks/common/data_io.h"

#define MATRIX_DIM 64
#define BLOCK_DIM 8
#define MATRIX_ELEMENTS MATRIX_DIM*MATRIX_DIM

ABSL_FLAG(bool, hex, false, "Display float data in hex format.");

constexpr const char kMatrixAPath[] = "xls/third_party/benchmarks/machsuite/gemm/test_data/a_matrix_64_64";
constexpr const char kMatrixBPath[] = "xls/third_party/benchmarks/machsuite/gemm/test_data/b_matrix_64_64";

namespace xls {
namespace {

// Note: This reference uses full IEEE-compliant floating-
// point operations, whereas the XLS version uses floating-
// point operations that do not support subnormal numbers.
// This version could be modified to flush subnormal numbers
// to guaruntee equivalence for all inputs. However, this is
// not necessary for the input tested.

void gemm(float m1[MATRIX_ELEMENTS], float m2[MATRIX_ELEMENTS], float prod[MATRIX_ELEMENTS]){
    for(int64_t i=0;i<MATRIX_DIM;i++) {
        for(int64_t j=0;j<MATRIX_DIM;j++) {
            int64_t i_col = i * MATRIX_DIM;
            float sum = 0;
            for(int64_t k=0;k<MATRIX_DIM;k++) {
                int64_t k_col = k * MATRIX_DIM;
                float mult = m1[i_col + k] * m2[k_col + j];
                sum += mult;
            }
            prod[i_col + j]  = sum;
        }
    }
}

void bbgemm(float m1[MATRIX_ELEMENTS], float m2[MATRIX_ELEMENTS], float prod[MATRIX_ELEMENTS]){
    for (int jj = 0; jj < MATRIX_DIM; jj += BLOCK_DIM){
        for (int kk = 0; kk < MATRIX_DIM; kk += BLOCK_DIM){
            for (int i = 0; i < MATRIX_DIM; ++i){
                for (int k = 0; k < BLOCK_DIM; ++k){
                    int i_row = i * MATRIX_DIM;
                    int k_row = (k  + kk) * MATRIX_DIM;
                    float temp_x = m1[i_row + k + kk];
                    for (int j = 0; j < BLOCK_DIM; ++j){
                        float mul = temp_x * m2[k_row + j + jj];
                        prod[i_row + j + jj] += mul;
                    }
                }
            }
        }
    }
}

absl::Status RealMain() {
  // Get input data.
  std::vector<float> a_matrix;
  std::vector<float> b_matrix;
  XLS_RETURN_IF_ERROR(read_float_data(kMatrixAPath, &a_matrix));
  XLS_RETURN_IF_ERROR(read_float_data(kMatrixBPath, &b_matrix));

  std::vector<float> result_matrix(MATRIX_ELEMENTS);
  gemm(&a_matrix[0], &b_matrix[0], &result_matrix[0]);
  bool display_hex = absl::GetFlag(FLAGS_hex);
  DisplayFloatData(result_matrix, "gemm_result_matrix", display_hex);

  std::vector<float> block_result_matrix(MATRIX_ELEMENTS);
  bbgemm(&a_matrix[0], &b_matrix[0], &block_result_matrix[0]);
  DisplayFloatData(block_result_matrix, "bbgemm_result_matrix", display_hex);

  return absl::OkStatus();
}

}  // namespace
}  // namespace xls

int main(int argc, char** argv) {
  xls::InitXls(argv[0], argc, argv);
  XLS_QCHECK_OK(xls::RealMain());
  return 0;
}
