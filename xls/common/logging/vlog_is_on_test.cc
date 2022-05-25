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

#include "xls/common/logging/vlog_is_on.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace xls {
namespace {

class VlogIsOnTest : public ::testing::Test {
 public:
  void ExpectVlogLevel(int level) {
    EXPECT_TRUE(XLS_VLOG_IS_ON(level));
    EXPECT_FALSE(XLS_VLOG_IS_ON(level + 1));
  }
};

TEST_F(VlogIsOnTest, GlobalFlagIsUsedByDefault) {
  absl::SetFlag(&FLAGS_xls_v, 4);
  ExpectVlogLevel(4);
}

TEST_F(VlogIsOnTest, MismatchingModuleNamePatternDoesNotApply) {
  absl::SetFlag(&FLAGS_xls_v, 5);
  SetVLOGLevel("something_else", 10);
  ExpectVlogLevel(5);
}

TEST_F(VlogIsOnTest, MismatchingModuleDirectoryWildcardPatternDoesNotApply) {
  absl::SetFlag(&FLAGS_xls_v, 5);
  SetVLOGLevel("*/other_dir/*", 10);
  ExpectVlogLevel(5);
}

TEST_F(VlogIsOnTest, ModuleNamePatternApplies) {
  absl::SetFlag(&FLAGS_xls_v, 5);
  SetVLOGLevel("vlog_is_on_test", 10);
  ExpectVlogLevel(10);
}

TEST_F(VlogIsOnTest, ModuleNameWithWildcardPatternApplies) {
  absl::SetFlag(&FLAGS_xls_v, 5);
  SetVLOGLevel("vlog_is_on*", 10);
  ExpectVlogLevel(10);
}

TEST_F(VlogIsOnTest, ModuleNameWithDirectoryWildcardPatternApplies) {
  absl::SetFlag(&FLAGS_xls_v, 5);
  SetVLOGLevel("*/common/*", 10);
  ExpectVlogLevel(10);
}

}  // namespace
}  // namespace xls
