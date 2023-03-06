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

absl::Status AstMutation::VisitIfStmt(VisitIfStmt *if_stmt) {
  Expr *cond = if_stmt->condition();
  Stmt *then_block = if_stmt->then_block();
  if (rand() <= options_.if_opt->shrink_rate) {
    // Shrink to a Stmt without condition.
    cond_buffer_.insert(cond);
    XLS_CHECK(if_stmt->parent()->ReplaceChild(if_stmt, then_block));
  } else if (stmt_buffer_.size() > 0 &&
             rand() <= options_.if_opt->extend_rate) {
    // Extend it to an IfElseStmt (if the stmt buffer is not empty).
    auto stmt_it = stmt_buffer_.begin();
    Stmt *new_stmt = node->module()->AddIfElseStmt(cond, then_block, *stmt_it);
    XLS_CHECK(if_stmt->parent()->ReplaceChild(if_stmt, new_stmt));
    stmt_buffer_.erase(stmt_it);
  } else {
    rhs_buffer_.insert(cond);
  }
  return absl::OkStatus();
}

absl::Status AstMutation::VisitIfElseStmt(IfElseStmt *if_else_stmt) {
  Expr *cond = if_else_stmt->condition();
  Stmt *then_block = if_else_stmt->then_block();
  Stmt *else_block = if_else_stmt->else_block();
  Stmt *new_stmt = nullptr;
  if (rand() <= options_.if_else_opt->remove_else_rate) {
    new_stmt = node->module()->AddIfStmt(cond, then_block);
    stmt_buffer_.insert(else_block);
  } else if (rand() <= options_.if_else_opt->reverse_clauses_rate) {
    new_stmt = node->module()->AddIfElseStmt(cond, else_block, then_block);
  }
  XLS_CHECK(if_else_stmt->parent()->ReplaceChild(if_else_stmt, new_node));
  return absl::OkStatus();
}

} // namespace xls::p5