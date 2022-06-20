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

#include "xls/passes/dce_pass.h"

#include "absl/status/statusor.h"
#include "xls/common/logging/logging.h"
#include "xls/common/status/status_macros.h"
#include "xls/ir/dfs_visitor.h"
#include "xls/ir/node_iterator.h"
#include "xls/ir/op.h"
#include "xls/ir/type.h"

namespace xls {

absl::StatusOr<bool> RunDce(FunctionBase* f, bool dry_run,
                            absl::flat_hash_set<Node*>* deleted) {
  auto is_deletable = [](Node* n) {
    return !n->function_base()->HasImplicitUse(n) &&
           !OpIsSideEffecting(n->op());
  };

  std::deque<Node*> worklist;
  for (Node* n : f->nodes()) {
    if (n->users().empty() && is_deletable(n)) {
      worklist.push_back(n);
    }
  }
  int64_t removed_count = 0;
  absl::flat_hash_set<Node*> unique_operands;
  while (!worklist.empty()) {
    Node* node = worklist.front();
    worklist.pop_front();

    // A node may appear more than once as an operand of 'node'. Keep track of
    // which operands have been handled in a set.
    unique_operands.clear();
    for (Node* operand : node->operands()) {
      if (unique_operands.insert(operand).second) {
        if (operand->users().size() == 1 && is_deletable(operand)) {
          worklist.push_back(operand);
        }
      }
    }
    if (!dry_run) {
      XLS_VLOG(3) << "DCE removing " << node->ToString();
      XLS_RETURN_IF_ERROR(f->RemoveNode(node));
      removed_count++;
    }
    if (deleted != nullptr) {
      deleted->insert(node);
    }
  }

  XLS_VLOG(2) << "Removed " << removed_count << " dead nodes";
  return removed_count > 0;
}

absl::StatusOr<bool> DeadCodeEliminationPass::RunOnFunctionBaseInternal(
    FunctionBase* f, const PassOptions& options, PassResults* results) const {
  return RunDce(f, false, nullptr);
}

}  // namespace xls
