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

#include "absl/base/log_severity.h"
#include "absl/flags/declare.h"

ABSL_DECLARE_FLAG(int, xls_minloglevel);
ABSL_DECLARE_FLAG(bool, xls_logtostderr);
ABSL_DECLARE_FLAG(bool, xls_alsologtostderr);
ABSL_DECLARE_FLAG(int, xls_stderrthreshold);
ABSL_DECLARE_FLAG(bool, xls_log_prefix);

namespace absl {

absl::LogSeverityAtLeast StderrThreshold();

}  // namespace absl
