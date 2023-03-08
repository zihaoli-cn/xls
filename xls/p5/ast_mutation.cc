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

absl::Status AstMutation::VisitIfStmt(IfStmt *if_stmt) {
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
    Stmt *new_stmt =
        if_stmt->module()->AddIfElseStmt(cond, then_block, *stmt_it);
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
    new_stmt = if_else_stmt->module()->AddIfStmt(cond, then_block);
    stmt_buffer_.insert(else_block);
  } else if (rand() <= options_.if_else_opt->reverse_clauses_rate) {
    new_stmt =
        if_else_stmt->module()->AddIfElseStmt(cond, else_block, then_block);
  }
  XLS_CHECK(if_else_stmt->parent()->ReplaceChild(if_else_stmt, new_stmt));
  return absl::OkStatus();
}

absl::Status AstMutation::VisitStmtBlock(StmtBlock *node) {
  std::vector<Stmt *> buffer;
  for (Stmt *stmt : node->stmts()) {
    if (rand() > options_.block_opt->remove_rate) {
      buffer.push_back(stmt);
    }

    if (stmt_buffer_.size() > 0 &&
        rand() <= options_.block_opt->insert_from_buffer_rate) {
      auto stmt_it = stmt_buffer_.begin();
      buffer.push_back(*stmt_it);
      stmt_buffer_.erase(stmt_it);
    } else if (lhs_buffer_.size() > 0 && rhs_buffer_.size() > 0 &&
               rand() <= options_.block_opt->insert_assign_rate) {
      auto lhs_it = lhs_buffer_.begin();
      auto rhs_it = rhs_buffer_.begin();

      AssignStmt *new_node = node->module()->AddAssignStmt(*lhs_it, *rhs_it);
      buffer.push_back(new_node);

      lhs_buffer_.erase(lhs_it);
      rhs_buffer_.erase(rhs_it);
    } else if (cond_buffer_.size() > 0 && stmt_buffer_.size() &&
               rand() <= options_.block_opt->insert_if_rate) {
      auto cond_it = cond_buffer_.begin();
      auto stmt_it = stmt_buffer_.begin();

      IfStmt *new_node = node->module()->AddIfStmt(*cond_it, *stmt_it);
      buffer.push_back(new_node);

      cond_buffer_.erase(cond_it);
      stmt_buffer_.erase(stmt_it);
    }
  }
  if (rand() <= options_.block_opt->insert_ret_rate) {
    buffer.push_back(node->module()->AddReturnStmt());
  }
  node->ReplaceAll(buffer);
  return absl::OkStatus();
}

} // namespace xls::p5