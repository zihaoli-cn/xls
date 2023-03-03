#pragma once
#include "absl/status/status.h"
#include "absl/types/optional.h"

#include "xls/common/logging/logging.h"

#include "xls/p5/ast.h"

#include <memory>
#include <random>
#include <set>

namespace xls::p5 {

/*
 Possible Mutation:
 1. StmtBlock:
    - randomly remove each stmt, if so, save that stmt to stmt buffer
    - insert a stmt from stmt buffer
    - insert a new AssignStmt
        - LHS: pick a lhs from lhs buffer
        - RHS:
          - if size(rhs buffer) >= 2, pick two rhs from rhs buffer, create a
            random BinaryOpExpr
          - if size(rhs buffer) < 2, use that rhs directly
    - randomly insert a return instruction at the end
    - randomly insert an IfStmt (if the condition buffer > 0) at random place
 2. IfElseStmt:
    - negate the banch condition
    - remove else-clause, add that stmt to stmt buffer
    - reverse then-clause and else-clause
 3. IfStmt:
    - shrink to a StmtBlock, save condition to condition buffer
 4. AssignStmt:
    - save active rhs to the rhs buffer
    - randomly replace lhs/rhs to currently active set of lhs/rhs
 5. Lvalue:
    - save active lhs to the lhs buffer

 Configuration:

 expected-stmt-num

 */

struct IfElseStmtOptions {
  int negate_rate;
  int remove_else_rate;
  int reverse_clauses_rate;

  explicit IfElseStmtOptions(int negate_rate, int remove_else_rate,
                             int reverse_clauses_rate)
      : negate_rate(negate_rate), remove_else_rate(remove_else_rate),
        reverse_clauses_rate(reverse_clauses_rate) {}
};

struct IfStmtOptions {
  int shrink_rate;

  explicit IfStmtOptions(int shrink_rate) : shrink_rate(shrink_rate) {}
};

struct AssignStmtOptions {
  int replace_rate;

  explicit AssignStmtOptions(int replace_rate) : replace_rate(replace_rate) {}
};

//  uint64_t expected_active_stmt;
//  uint64_t max_return_num;

class MutaionOptions;
class MutaionOptionsBuilder;

class MutaionOptions {
public:
  int precision_factor;
  absl::optional<IfElseStmtOptions> if_else_opt;
  absl::optional<IfStmtOptions> if_opt;
  absl::optional<AssignStmtOptions> assign_opt;

  MutaionOptions(int precision_factor) : precision_factor(precision_factor) {}
};

class MutaionOptionsBuilder {
  typedef MutaionOptionsBuilder self;
  MutaionOptions opt_;

public:
  explicit MutaionOptionsBuilder(int precision_factor)
      : opt_(precision_factor) {}

  operator MutaionOptions() { return std::move(opt_); }

  self &SupportIf(int shrink_rate) {
    XLS_CHECK(shrink_rate <= opt_.precision_factor);
    opt_.if_opt = IfStmtOptions(shrink_rate);
    return *this;
  }

  self &SupportIfElse(int negate_rate, int remove_else_rate,
                      int reverse_clauses_rate) {
    XLS_CHECK(negate_rate <= opt_.precision_factor &&
              remove_else_rate <= opt_.precision_factor &&
              reverse_clauses_rate <= opt_.precision_factor);
    opt_.if_else_opt =
        IfElseStmtOptions(negate_rate, remove_else_rate, reverse_clauses_rate);
    return *this;
  }

  self &SupportAssign(int replace_rate) {
    XLS_CHECK(replace_rate <= opt_.precision_factor);
    opt_.assign_opt = AssignStmtOptions(replace_rate);
    return *this;
  }
};

class AstMutation : public VisitAll {
public:
  absl::Status VisitModule(Module *node) override;
  absl::Status VisitStmtBlock(StmtBlock *node) override;
  absl::Status VisitIfStmt(IfStmt *node) override;
  absl::Status VisitIfElseStmt(IfElseStmt *node) override;
  absl::Status VisitAssignStmt(AssignStmt *node) override;
  absl::Status VisitLvalue(Lvalue *) override;

private:
  std::random_device rd_;

  std::set<Stmt *> stmt_buffer_;
  std::set<Lvalue *> lhs_buffer_;
  std::set<Expr *> rhs_buffer_;
  std::set<Expr *> cond_buffer_;
};

} // namespace xls::p5