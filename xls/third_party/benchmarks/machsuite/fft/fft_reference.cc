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

#define FFT_SIZE 1024

ABSL_FLAG(bool, hex, false, "Display float data in hex format.");

constexpr const char kRealInPath[] = "xls/third_party/benchmarks/machsuite/fft/test_data/real_in";
constexpr const char kImgInPath[] = "xls/third_party/benchmarks/machsuite/fft/test_data/img_in";
constexpr const char kRealTwidInPath[] = "xls/third_party/benchmarks/machsuite/fft/test_data/real_twid_in";
constexpr const char kImgTwidInPath[] = "xls/third_party/benchmarks/machsuite/fft/test_data/img_twid_in";

namespace xls {
namespace {

// Note: This reference uses full IEEE-compliant floating-
// point operations, whereas the XLS version uses floating-
// point operatoins that do not support subnormal numbers.
// This version could be modified to flush subnormal numbers
// to guaruntee equivalence for all inputs. However, this is
// not necessary for the input tested.

void fft(float real[FFT_SIZE], float img[FFT_SIZE], float real_twid[FFT_SIZE/2], float img_twid[FFT_SIZE/2]){
    int even, odd, span, log, rootindex;
    float temp;
    log = 0;

    for(span=FFT_SIZE>>1; span; span>>=1, log++){
        for(odd=span; odd<FFT_SIZE; odd++){
            odd |= span;
            even = odd ^ span;

            temp = real[even] + real[odd];
            real[odd] = real[even] - real[odd];
            real[even] = temp;

            temp = img[even] + img[odd];
            img[odd] = img[even] - img[odd];
            img[even] = temp;

            rootindex = (even<<log) & (FFT_SIZE - 1);
            if(rootindex){
                temp = real_twid[rootindex] * real[odd] -
                    img_twid[rootindex]  * img[odd];
                img[odd] = real_twid[rootindex]*img[odd] +
                    img_twid[rootindex]*real[odd];
                real[odd] = temp;
            }
        }
    }
}

absl::Status RealMain() {
  // Get input data.
  std::vector<float> real;
  std::vector<float> img;
  std::vector<float> real_twid;
  std::vector<float> img_twid;
  XLS_RETURN_IF_ERROR(read_float_data(kRealInPath, &real));
  XLS_RETURN_IF_ERROR((read_float_data(kImgInPath, &img)));
  XLS_RETURN_IF_ERROR((read_float_data(kRealTwidInPath, &real_twid)));
  XLS_RETURN_IF_ERROR((read_float_data(kImgTwidInPath, &img_twid)));

  fft(&real[0], &img[0], &real_twid[0], &img_twid[0]);
  bool display_hex = absl::GetFlag(FLAGS_hex);
  DisplayFloatData(real, "real_out", display_hex);
  DisplayFloatData(img, "img_out", display_hex);

  return absl::OkStatus();
}

}  // namespace
}  // namespace xls

int main(int argc, char** argv) {
  xls::InitXls(argv[0], argc, argv);
  XLS_QCHECK_OK(xls::RealMain());
  return 0;
}
