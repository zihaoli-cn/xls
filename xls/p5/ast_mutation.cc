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
  XLS_RETURN_IF_ERROR(VisitLvalue(node->lhs()));
  XLS_RETURN_IF_ERROR(VisitExpr(node->rhs()));

  rhs_buffer_.insert(node->rhs());

  return absl::OkStatus();
}
} // namespace xls::p5