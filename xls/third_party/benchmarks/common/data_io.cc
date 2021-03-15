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

#include "xls/third_party/benchmarks/common/data_io.h"

#include <cstdio>
#include <filesystem>
#include <iostream>

#include "absl/strings/str_split.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "xls/common/file/filesystem.h"
#include "xls/common/file/get_runfile_path.h"
#include "xls/common/init_xls.h"
#include "xls/common/logging/logging.h"
#include "xls/common/status/status_macros.h"

using xls::GetXlsRunfilePath;
using xls::GetFileContents;

namespace xls {

absl::Status read_float_data(absl::string_view in_path, std::vector<float>* data, bool get_runfile_path) {
  std::filesystem::path file_path;
  if(get_runfile_path) {
    XLS_ASSIGN_OR_RETURN(file_path,
                         GetXlsRunfilePath(in_path));
  } else {
    file_path = in_path;
  }
  XLS_ASSIGN_OR_RETURN(std::string data_text, 
                       GetFileContents(file_path));

  data->clear();
  for (auto line : absl::StrSplit(data_text, '\n')) {
    if(line.size()) {
      data->push_back(0.0);
      XLS_CHECK(absl::SimpleAtof(line, &data->back()));
    }
  }

  return absl::OkStatus();
}

union float_uint_union {
  float f32;
  uint32_t u32;
};

void DisplayFloatData(std::vector<float>& data, std::string name, bool display_hex) {
  std::cout << name << " = [" << std::endl;

  for(float data_item : data) {
    if(display_hex) {
      union float_uint_union data_union;
      data_union.f32 = data_item;
      std::cout << absl::StreamFormat("0x%x,\n", data_union.u32);
    } else {
      std::cout << data_item << "," << std::endl;
    }
  }

  std::cout << "]" << std::endl;
}

}  // namespace xls
