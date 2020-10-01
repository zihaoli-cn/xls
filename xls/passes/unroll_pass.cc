// Copyright 2020 Google LLC
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

#include "xls/passes/unroll_pass.h"

#include "absl/status/status.h"
#include "xls/common/status/status_macros.h"
#include "xls/ir/node_iterator.h"

namespace xls {
namespace {

// Finds an "effectively used" (has users or is return value) counted for in the
// function f, or returns nullptr if none is found.
CountedFor* FindCountedFor(Function* f) {
  for (Node* node : TopoSort(f)) {
    if (node->Is<CountedFor>() &&
        (node == f->return_value() || !node->users().empty())) {
      return node->As<CountedFor>();
    }
  }
  return nullptr;
}

// Unrolls the node "loop" by replacing it with a sequence of dependent
// invocations.
absl::Status UnrollCountedFor(CountedFor* loop, Function* f) {
  Node* loop_carry_in = loop->initial_value();
  int64 ivar_bit_count = loop->body()->params()[0]->BitCountOrDie();
  bool array_parallel = loop->IsArrayParallelizable();
  std::vector<Node*> parallel_array_elements;

  for (int64 trip = 0, iv = 0; trip < loop->trip_count();
       ++trip, iv += loop->stride()) {
    XLS_ASSIGN_OR_RETURN(
        Literal * iv_node,
        f->MakeNode<Literal>(loop->loc(), Value(UBits(iv, ivar_bit_count))));

    // Construct the args for invocation.
    std::vector<Node*> invoke_args = {iv_node, loop_carry_in};
    for (Node* invariant_arg : loop->invariant_args()) {
      invoke_args.push_back(invariant_arg);
    }

    XLS_ASSIGN_OR_RETURN(
        Node* loop_carry_out,
        f->MakeNode<Invoke>(loop->loc(), absl::MakeSpan(invoke_args),
                            loop->body()));

    if(array_parallel) {
      XLS_ASSIGN_OR_RETURN(
          Node* element, f->MakeNode<ArrayIndex>(loop->loc(), loop_carry_out, iv_node));
      parallel_array_elements.push_back(element);
    } else {
      loop_carry_in = loop_carry_out;
    }
  }

  if(array_parallel) {
    XLS_ASSIGN_OR_RETURN(Node* result_array, f->MakeNode<Array>(loop->loc(), parallel_array_elements, loop_carry_in->GetType()->AsArrayOrDie()->element_type()));
    XLS_RETURN_IF_ERROR(loop->ReplaceUsesWith(result_array).status());
  } else {
    XLS_RETURN_IF_ERROR(loop->ReplaceUsesWith(loop_carry_in).status());
  }
  return f->RemoveNode(loop);
}

}  // namespace

absl::StatusOr<bool> UnrollPass::RunOnFunction(Function* f,
                                               const PassOptions& options,
                                               PassResults* results) const {
  bool changed = false;
  while (true) {
    CountedFor* loop = FindCountedFor(f);
    if (loop == nullptr) {
      break;
    }
    // Recursively unroll loops within loop body.
    XLS_ASSIGN_OR_RETURN(changed, RunOnFunction(loop->body(), options, results));
    XLS_RETURN_IF_ERROR(UnrollCountedFor(loop, f));
    changed = true;
  }
  return changed;
}

}  // namespace xls
