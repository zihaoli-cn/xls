#include "absl/status/status.h"
#include "absl/strings/str_format.h"

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
    XLS_RETURN_IF_ERROR(VisitLvalue(node->lhs()));
    XLS_RETURN_IF_ERROR(VisitExpr(node->rhs()));
    return absl::OkStatus();
  }

  if (lhs_buffer_.size() > 0 && rhs_buffer_.size() > 0 &&
      rand() <= options_.assign_opt->replace_rate) {
    auto lhs_it = lhs_buffer_.begin();
    auto rhs_it = rhs_buffer_.begin();

    AssignStmt *new_node = node->module()->AddAssignStmt(*lhs_it, *rhs_it);
    XLS_CHECK(node->parent()->ReplaceChild(node, new_node));

    lhs_buffer_.erase(lhs_it);
    rhs_buffer_.erase(rhs_it);

    lhs_buffer_.insert(node->lhs());
    rhs_buffer_.insert(node->rhs());

    std::cerr << absl::StrFormat(
                     "LOG: AstMutation::VisitAssignStmt, replace %s with %s",
                     node->ToString(), new_node->ToString())
              << std::endl;
  } else {
    std::cerr << absl::StrFormat(
                     "LOG: AstMutation::VisitAssignStmt, %s is not replaced",
                     node->ToString())
              << std::endl;

    lhs_buffer_.insert(node->lhs());
    rhs_buffer_.insert(node->rhs());

    XLS_RETURN_IF_ERROR(VisitLvalue(node->lhs()));
    XLS_RETURN_IF_ERROR(VisitExpr(node->rhs()));
  }

  return absl::OkStatus();
}

absl::Status AstMutation::VisitIfStmt(IfStmt *if_stmt) {
  if (options_.if_opt == absl::nullopt) {
    rhs_buffer_.insert(if_stmt->condition());
    stmt_buffer_.insert(if_stmt->then_block());

    XLS_RETURN_IF_ERROR(VisitExpr(if_stmt->condition()));
    XLS_RETURN_IF_ERROR(VisitStmt(if_stmt->then_block()));
    return absl::OkStatus();
  }

  Expr *cond = if_stmt->condition();
  Stmt *then_block = if_stmt->then_block();
  if (rand() <= options_.if_opt->shrink_rate) {
    std::cerr
        << absl::StrFormat(
               "LOG: AstMutation::VisitIfStmt, IF->BLOCK, shrink %s to %s",
               if_stmt->ToString(), then_block->ToString())
        << std::endl;

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

    std::cerr
        << absl::StrFormat(
               "LOG: AstMutation::VisitIfStmt, IF->IFELSE, extend %s to %s",
               if_stmt->ToString(), new_stmt->ToString())
        << std::endl;
  } else {
    rhs_buffer_.insert(cond);
  }
  return absl::OkStatus();
}

absl::Status AstMutation::VisitIfElseStmt(IfElseStmt *if_else_stmt) {
  if (options_.if_else_opt == absl::nullopt) {
    rhs_buffer_.insert(if_else_stmt->condition());
    stmt_buffer_.insert(if_else_stmt->then_block());

    XLS_RETURN_IF_ERROR(VisitExpr(if_else_stmt->condition()));
    XLS_RETURN_IF_ERROR(VisitStmt(if_else_stmt->then_block()));
    XLS_RETURN_IF_ERROR(VisitStmt(if_else_stmt->else_block()));
    return absl::OkStatus();
  }

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
  if (options_.block_opt == absl::nullopt) {
    for (Stmt *stmt : node->stmts()) {
      stmt_buffer_.insert(stmt);
      XLS_RETURN_IF_ERROR(VisitStmt(stmt));
    }
    return absl::OkStatus();
  }

  std::vector<Stmt *> buffer;
  for (Stmt *stmt : node->stmts()) {
    if (rand() > options_.block_opt->remove_rate) {
      buffer.push_back(stmt);
    } else {
      std::cerr << "REMOVE stmt from block: " << stmt->ToString() << std::endl;
    }

    if (stmt_buffer_.size() > 0 &&
        rand() <= options_.block_opt->insert_from_buffer_rate) {
      auto stmt_it = stmt_buffer_.begin();
      buffer.push_back(*stmt_it);

      std::cerr << "INSERT stmt from buffer : " << (*stmt_it)->ToString()
                << std::endl;

      stmt_buffer_.erase(stmt_it);
    } else if (lhs_buffer_.size() > 0 && rhs_buffer_.size() > 0 &&
               rand() <= options_.block_opt->insert_assign_rate) {
      auto lhs_it = lhs_buffer_.begin();
      auto rhs_it = rhs_buffer_.begin();

      AssignStmt *new_node = node->module()->AddAssignStmt(*lhs_it, *rhs_it);
      buffer.push_back(new_node);

      lhs_buffer_.erase(lhs_it);
      rhs_buffer_.erase(rhs_it);

      std::cerr << "INSERT assign stmt into block : " << new_node->ToString()
                << std::endl;

    } else if (cond_buffer_.size() > 0 && stmt_buffer_.size() &&
               rand() <= options_.block_opt->insert_if_rate) {
      auto cond_it = cond_buffer_.begin();
      auto stmt_it = stmt_buffer_.begin();

      IfStmt *new_node = node->module()->AddIfStmt(*cond_it, *stmt_it);
      buffer.push_back(new_node);

      cond_buffer_.erase(cond_it);
      stmt_buffer_.erase(stmt_it);

      std::cerr << "INSERT if stmt into block : " << new_node->ToString()
                << std::endl;
    }
  }
  if (rand() <= options_.block_opt->insert_ret_rate) {
    buffer.push_back(node->module()->AddReturnStmt());
  }
  node->ReplaceAll(buffer);
  return absl::OkStatus();
}

absl::Status AstMutation::VisitModule(Module *m) {
  return VisitStmtBlock(m->body);
}
} // namespace xls::p5