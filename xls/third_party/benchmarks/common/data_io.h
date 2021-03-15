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

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace xls {

// Load 'data' with the floats contained the file pointed at
// location 'path'. Expects exactly one float per line.
// If 'get_runfile_path' is true, 'path' interpreted as relative to the
// root XLS source directory and this function opens the file in the Bazel
// runfiles directory.  Otherwise, 'path' is used directly to find the file.
absl::Status read_float_data(absl::string_view path, std::vector<float>* data,
    bool get_runfile_path=true);

// Display the contents of 'data' with name 'name' and 
// 'size' number of entries.  If 'display_hex' is true,
// the the hex contents of each float will be displayed.  Otherwise,
// the data will be displayed in decimal form.
void DisplayFloatData(std::vector<float>& data, std::string name, bool display_hex);

}  // namespace xls
