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

#include "xls/passes/cse_pass.h"

#include "absl/hash/hash.h"
#include "absl/status/statusor.h"
#include "xls/common/logging/logging.h"
#include "xls/common/status/status_macros.h"
#include "xls/ir/node_iterator.h"
#include "xls/ir/node_util.h"
#include "xls/ir/op.h"

namespace xls {

namespace {

// Returns the operands of the given node for the purposes of the CSE
// optimization. The order of the nodes may not match the order of the node's
// actual operands. Motivation: generally for nodes to be considered equivalent
// the operands must be in the same order. However, commutative operations are
// agnostic to operand order. So to expand the CSE optimization, compare
// operands as an unordered set for commutative operands. This is done be
// ordered commutative operation operands by id prior to comparison. To avoid
// having to construct a vector every time, a span is returned by this
// function. If the operation is not commutative, the node's own operand span is
// simply returned. For the commutative case, a vector of sorted operands is
// constructed in span_backing_store from which a span is constructed.
absl::Span<Node* const> GetOperandsForCse(
    Node* node, std::vector<Node*>* span_backing_store) {
  XLS_CHECK(span_backing_store->empty());
  if (!OpIsCommutative(node->op())) {
    return node->operands();
  }
  span_backing_store->insert(span_backing_store->begin(),
                             node->operands().begin(), node->operands().end());
  SortByNodeId(span_backing_store);
  return *span_backing_store;
}

}  // namespace

absl::StatusOr<bool> RunCse(
    FunctionBase* f, absl::flat_hash_map<Node*, Node*>* replacements,
    const absl::flat_hash_map<Node*, int64_t>& mergeable) {
  // To improve efficiency, bucket potentially common nodes together. The
  // bucketing is done via an int64_t hash value which is constructed from the
  // op() of the node and the uid's of the node's operands.
  auto hasher = absl::Hash<std::vector<int64_t>>();
  auto node_hash = [&](Node* n) {
    std::vector<int64_t> values_to_hash = {static_cast<int64_t>(n->op())};
    std::vector<Node*> span_backing_store;
    for (Node* operand : GetOperandsForCse(n, &span_backing_store)) {
      values_to_hash.push_back(operand->id());
    }
    // If this is slow because of many literals, the Literal values could be
    // combined into the hash. As is, all literals get the same hash value.
    return hasher(values_to_hash);
  };

  // Determines whether two nodes are mergeable based on the mergeable_ member.
  auto is_mergeable = [&](Node* x, Node* y) -> bool {
    if (!mergeable.contains(x) && !mergeable.contains(y)) {
      return true;
    }
    if (mergeable.contains(x) && mergeable.contains(y)) {
      return mergeable.at(x) == mergeable.at(y);
    }
    return false;
  };

  bool changed = false;
  absl::flat_hash_map<int64_t, std::vector<Node*>> node_buckets;
  node_buckets.reserve(f->node_count());
  for (Node* node : TopoSort(f)) {
    if (OpIsSideEffecting(node->op())) {
      continue;
    }

    int64_t hash = node_hash(node);
    if (!node_buckets.contains(hash)) {
      node_buckets[hash].push_back(node);
      continue;
    }
    bool replaced = false;
    std::vector<Node*> node_span_backing_store;
    absl::Span<Node* const> node_operands_for_cse =
        GetOperandsForCse(node, &node_span_backing_store);
    for (Node* candidate : node_buckets.at(hash)) {
      std::vector<Node*> candidate_span_backing_store;
      if (node_operands_for_cse ==
              GetOperandsForCse(candidate, &candidate_span_backing_store) &&
          node->IsDefinitelyEqualTo(candidate) &&
          is_mergeable(node, candidate)) {
        XLS_VLOG(3) << absl::StreamFormat(
            "Replacing %s with equivalent node %s", node->GetName(),
            candidate->GetName());
        XLS_RETURN_IF_ERROR(node->ReplaceUsesWith(candidate));
        if (replacements != nullptr) {
          (*replacements)[node] = candidate;
        }
        changed = true;
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      node_buckets[hash].push_back(node);
    }
  }

  return changed;
}

absl::StatusOr<bool> CsePass::RunOnFunctionBaseInternal(
    FunctionBase* f, const PassOptions& options, PassResults* results) const {
  return RunCse(f, nullptr);
}

}  // namespace xls
