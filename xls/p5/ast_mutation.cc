#include "absl/status/status.h"

#include "xls/common/logging/logging.h"
#include "xls/common/status/status_macros.h"
#include "xls/p5/ast_mutation.h"

#include <vector>

namespace xls::p5 {

absl::Status AstMutation::VisitLvalue(Lvalue *lhs) {
  lhs_buffer_.insert(lhs);
  return VisitAll::VisitLvalue(lhs);
}

absl::Status AstMutation::VisitAssignStmt(AssignStmt *node) {
  if (options_.assign_opt == absl::nullopt) {
    return VisitAll::VisitAssignStmt(node);
  }

  Expr *rhs = node->rhs();
  if (lhs_buffer_.size() > 0 && rhs_buffer_.size() > 0 &&
      rand() <= options_.assign_opt->replace_rate) {
    auto lhs_it = lhs_buffer_.begin();
    auto rhs_it = rhs_buffer_.begin();

    AssignStmt *new_node = node->module()->AddAssignStmt(*lhs_it, *rhs_it);
    XLS_CHECK(node->parent()->ReplaceChild(node, new_node));

    lhs_buffer_.erase(lhs_it);
    rhs_buffer_.erase(rhs_it);
  } else {
    XLS_RETURN_IF_ERROR(VisitLvalue(node->lhs()));
    XLS_RETURN_IF_ERROR(VisitExpr(node->rhs()));
  }
  rhs_buffer_.insert(rhs);

  return absl::OkStatus();
}
} // namespace xls::p5