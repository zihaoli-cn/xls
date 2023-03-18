#pragma once
#include "absl/status/status.h"
#include "absl/types/optional.h"

#include "xls/common/logging/logging.h"

#include "xls/p5/ast.h"

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <random>
#include <set>

namespace xls::p5 {
/*
 Possible Mutation:
 1. StmtBlock:
    - for each element stmt, randomly remove it, if it is removed, save that
        stmt to stmt buffer
    - if the stmt buffer is not empty, insert a stmt from stmt buffer
    - insert a new AssignStmt
        - LHS: pick a lhs from lhs buffer
        - RHS:
          - if size(rhs buffer) >= 2, pick two rhs from rhs buffer, create a
            random BinaryOpExpr
          - if size(rhs buffer) < 2, use that rhs directly
    - randomly insert an IfStmt (if the condition buffer > 0) at random place
    - randomly insert a return instruction at the end
 2. IfElseStmt:
    - negate the banch condition
    - remove else-clause, add that stmt to stmt buffer
    - reverse then-clause and else-clause
 3. IfStmt:
    - shrink to a StmtBlock, save condition to condition buffer
    - extend to a IfElseBlock, if the stmt buffer is not empty
 4. AssignStmt:
    - save active rhs to the rhs buffer
    - randomly replace lhs/rhs to currently active set of lhs/rhs
 5. Lvalue:
    - save active lhs to the lhs buffer

 Configuration:

 expected-stmt-num

 */
struct StmtBlockOptions {
  int remove_rate;
  int insert_from_buffer_rate;
  int insert_assign_rate;
  int insert_ret_rate;
  int insert_if_rate;

  explicit StmtBlockOptions(int remove_rate, int insert_from_buffer_rate,
                            int insert_assign_rate, int insert_ret_rate,
                            int insert_if_rate)
      : remove_rate(remove_rate),
        insert_from_buffer_rate(insert_from_buffer_rate),
        insert_assign_rate(insert_assign_rate),
        insert_ret_rate(insert_ret_rate), insert_if_rate(insert_if_rate) {}
};

struct IfElseStmtOptions {
  int remove_else_rate;
  int reverse_clauses_rate;

  explicit IfElseStmtOptions(int remove_else_rate, int reverse_clauses_rate)
      : remove_else_rate(remove_else_rate),
        reverse_clauses_rate(reverse_clauses_rate) {}
};

struct IfStmtOptions {
  int shrink_rate;
  int extend_rate;

  explicit IfStmtOptions(int shrink_rate, int extend_rate)
      : shrink_rate(shrink_rate), extend_rate(extend_rate) {}
};

struct AssignStmtOptions {
  int replace_rate;

  explicit AssignStmtOptions(int replace_rate) : replace_rate(replace_rate) {}
};

//  uint64_t expected_active_stmt;
//  uint64_t max_return_num;

class MutaionOptions {
public:
  int precision_factor;
  absl::optional<StmtBlockOptions> block_opt = absl::nullopt;
  absl::optional<IfElseStmtOptions> if_else_opt = absl::nullopt;
  absl::optional<IfStmtOptions> if_opt = absl::nullopt;
  absl::optional<AssignStmtOptions> assign_opt = absl::nullopt;

  MutaionOptions(int precision_factor) : precision_factor(precision_factor) {}
};

class MutaionOptionsBuilder {
  typedef MutaionOptionsBuilder self;
  MutaionOptions opt_;

public:
  explicit MutaionOptionsBuilder(int precision_factor)
      : opt_(precision_factor) {}

  operator MutaionOptions() { return std::move(opt_); }

  self &SupportStmtBlock(int remove_rate, int insert_from_buffer_rate,
                         int insert_assign_rate, int insert_ret_rate,
                         int insert_if_rate) {
    XLS_CHECK(remove_rate <= opt_.precision_factor &&
              insert_from_buffer_rate <= opt_.precision_factor &&
              insert_assign_rate <= opt_.precision_factor &&
              insert_ret_rate <= opt_.precision_factor &&
              insert_if_rate <= opt_.precision_factor);
    opt_.block_opt =
        StmtBlockOptions(remove_rate, insert_from_buffer_rate,
                         insert_assign_rate, insert_ret_rate, insert_if_rate);
    return *this;
  }

  self &SupportIf(int shrink_rate, int extend_rate) {
    XLS_CHECK(shrink_rate <= opt_.precision_factor &&
              extend_rate <= opt_.precision_factor);
    opt_.if_opt = IfStmtOptions(shrink_rate, extend_rate);
    return *this;
  }

  self &SupportIfElse(int remove_else_rate, int reverse_clauses_rate) {
    XLS_CHECK(remove_else_rate <= opt_.precision_factor &&
              reverse_clauses_rate <= opt_.precision_factor);
    opt_.if_else_opt =
        IfElseStmtOptions(remove_else_rate, reverse_clauses_rate);
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
  /*int rand() {
    std::uniform_int_distribution<int> distrib(1, options_.precision_factor);

    int tmp = distrib(gen_);
    std::cerr << tmp << std::endl;
    return tmp;
  }*/

  int rand() {
    int tmp = 1 + (std::rand() % options_.precision_factor);
    std::cerr << tmp << std::endl;
    return tmp;
  }

  AstMutation(const MutaionOptions &options) : options_(options) {
    // std::srand(std::time(0));
  }

  absl::Status VisitModule(Module *node) override;
  absl::Status VisitStmtBlock(StmtBlock *node) override;
  absl::Status VisitIfStmt(IfStmt *node) override;
  absl::Status VisitIfElseStmt(IfElseStmt *node) override;
  absl::Status VisitAssignStmt(AssignStmt *node) override;
  absl::Status VisitLvalue(Lvalue *) override;

private:
  std::mt19937 gen_;
  const MutaionOptions &options_;

  std::set<Stmt *> stmt_buffer_;
  std::set<Lvalue *> lhs_buffer_;
  std::set<Expr *> rhs_buffer_;
  std::set<Expr *> cond_buffer_;
};

} // namespace xls::p5