#include "xls/p5/ir_converter.h"
#include "absl/container/inlined_vector.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "boost/icl/discrete_interval.hpp"
#include "boost/icl/interval_set.hpp"
#include "boost/icl/split_interval_set.hpp"
#include "xls/common/logging/logging.h"
#include "xls/ir/bits.h"
#include "xls/ir/function_builder.h"
#include "xls/ir/lsb_or_msb.h"
#include "xls/ir/source_location.h"
#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <map>
#include <set>
#include <vector>

namespace xls::p5 {
namespace {
// Base class for Doing analysis on AST
//   send the `AnalysisResultType` pointer to the constructor
//   call `Run` to conduct analysis
//   call `GetAnlysisResult` to get result
template <typename AnalysisResultType>
class AstAnalysisVisitor : public VisitAll {
public:
  AstAnalysisVisitor(AnalysisResultType *result) : result_(result) {}

  virtual absl::Status Run(StmtBlock *func_body) {
    return VisitStmtBlock(func_body);
  }

  AnalysisResultType *GetAnlysisResult() { return result_; }

protected:
  AnalysisResultType *result_;
};

// You can combine 2 nested IfStmt, here is an example:
// if(e1){    | if(e1 && e2){
//   if(e2){  |   stmt..
//     stmt.. | }
//   }        |
// }          |
class NestedIfStmtElimination : public VisitAll {
public:
  absl::Status VisitModule(Module *module) override {
    changed_ = true;
    while (changed_) {
      changed_ = false;
      XLS_RETURN_IF_ERROR(VisitStmtBlock(module->body));
    }
    return absl::OkStatus();
  }

  absl::Status VisitIfStmt(IfStmt *stmt) override {
    // XLS_LOG_IF(FATAL, !(stmt && stmt->parent())) << stmt->ToString(2, 0);
    XLS_CHECK((stmt && stmt->parent()));
    auto parent = stmt->parent();
    if (parent->GetKind() == AstNodeKind::kIfStmt &&
        ((IfStmt *)parent)->then_block() == stmt) {
      XLS_LOG(INFO) << "Find an oppotunity to merge Nested IfStmt, content: \n"
                    << parent->ToString(2, 0);
      // Merge 2 nested IfStmt
      auto old_if_stmt = (IfStmt *)parent;

      auto module = stmt->module();
      auto new_if_stmt = module->AddIfStmt(
          module->AddBinaryOpExpr(OpKind::kLogicalAnd, old_if_stmt->condition(),
                                  stmt->condition()),
          stmt->then_block());

      auto parent_parent = parent->parent();
      XLS_CHECK(old_if_stmt->parent()->ReplaceChild(old_if_stmt, new_if_stmt));
      new_if_stmt->SetParent(parent_parent);

      XLS_LOG(INFO) << "After merge, content: \n"
                    << new_if_stmt->ToString(2, 0);

      changed_ = true;
    } else {
      XLS_RETURN_IF_ERROR(VisitStmt(stmt->then_block()));
    }
    return absl::OkStatus();
  }

private:
  bool changed_ = false;
};

class NestedIfStmtEliminationVerify : public VisitAll {
public:
  absl::Status VisitIfStmt(IfStmt *stmt) override {
    auto parent = stmt->parent();
    XLS_CHECK((parent->GetKind() != AstNodeKind::kIfStmt) ||
              ((IfStmt *)parent)->then_block() != stmt);

    XLS_RETURN_IF_ERROR(VisitStmt(stmt->then_block()));
    return absl::OkStatus();
  }
};

class UselessBlockElimination : public VisitAll {
public:
  absl::Status VisitStmtBlock(StmtBlock *stmt) override {
    XLS_CHECK(stmt && stmt->parent());

    if (stmt->parent()->GetKind() == AstNodeKind::kStmtBlock) {
      XLS_LOG(INFO) << "Find oppotunity to unroll useless block:\n"
                    << stmt->ToString(2, 0);
      auto parent = (StmtBlock *)stmt->parent();
      std::vector<Stmt *> stmts;

      bool occurred = false;
      for (auto tmp : parent->stmts()) {
        if (tmp == stmt) {
          occurred = true;
          for (auto inner : stmt->stmts()) {
            stmts.push_back(inner);
          }
        } else {
          stmts.push_back(tmp);
        }
      }
      XLS_CHECK(occurred);

      auto module = stmt->module();
      auto new_stmtblock = module->AddStmtBlock("manually_added", stmts);
      auto parent_parent = parent->parent();
      XLS_CHECK(parent_parent->ReplaceChild(parent, new_stmtblock));
      new_stmtblock->SetParent(parent_parent);

      changed_ = true;
    } else {
      for (auto stmt_ptr : stmt->stmts()) {
        XLS_RETURN_IF_ERROR(VisitStmt((Stmt *)stmt_ptr));
      }
    }
    return absl::OkStatus();
  }

  absl::Status VisitModule(Module *module) override {
    changed_ = true;
    while (changed_) {
      changed_ = false;
      XLS_RETURN_IF_ERROR(VisitStmtBlock(module->body));
    }
    return absl::OkStatus();
  }

private:
  bool changed_;
};

class UselessBlockEliminationVerify : public VisitAll {
public:
  absl::Status VisitStmtBlock(StmtBlock *stmt) override {
    XLS_CHECK(stmt->parent()->GetKind() != AstNodeKind::kStmtBlock);
    return absl::OkStatus();
  }
};

// Collect those Field-Access Expr
struct FieldAccessLoweringInfo {
  using FieldAccessMap =
      std::map<FieldAccessExpr *,
               BitSliceExpr *>; // map orignal AST node to lowered AST node
  using NamingMap =
      std::map<BitSliceExpr *,
               std::string>; // map lowered AST node to a readable name

  std::set<FieldAccessExpr *>
      field_accesses; // orginal Field-Access Expr AST Node
  std::set<FakeVarDef *>
      defs; // generated fake variables' def. for struct object
  FieldAccessMap field_access_map;
  NamingMap readable_name_map; // give BitSliceExpr a readable name
};

// Convert `a.b.c.d` to variable `a`'s bit-slice
class FieldAccessElimination : public VisitAll {
public:
  FieldAccessElimination(FieldAccessLoweringInfo *lowering_info,
                         const std::string &delimiter,
                         xls::p5::LoweringMappingRel *mapping_ = nullptr)
      : lowering_info_(lowering_info), delimiter_(delimiter),
        mapping_(mapping_) {}

  absl::Status VisitFieldAccessExpr(FieldAccessExpr *expr) override;

private:
  FieldAccessLoweringInfo *lowering_info_;
  std::string delimiter_;
  xls::p5::LoweringMappingRel *mapping_;
};

// Verify that there is no `FieldAccessExpr`
class FieldAccessEliminationVerify : public VisitAll {
public:
  absl::Status VisitFieldAccessExpr(FieldAccessExpr *node) override;
};

std::string GenerateName(FieldAccessExpr *expr, const std::string &delimiter) {
  auto kind = expr->source()->GetKind();
  std::string suffix = delimiter + std::string(expr->field_name());
  if (kind == AstNodeKind::kFieldAccessExpr) {
    return GenerateName((FieldAccessExpr *)expr->source(), delimiter) + suffix;
  } else if (expr->source()->GetKind() == AstNodeKind::kNameRefExpr) {
    return std::string(((NameRefExpr *)expr->source())->name()) + suffix;
  } else {
    XLS_LOG(FATAL) << "unsupported source type, "
                      "AstNode content : "
                   << expr->source()->ToString(2, 0);
    return ""; // never get here
  }
}

std::pair<std::string, uint32_t>
GetInnerStructVarAnnotation(FieldAccessExpr *expr) {
  auto source = expr->source();
  XLS_CHECK(source->GetKind() == AstNodeKind::kNameRefExpr ||
            source->GetKind() == AstNodeKind::kFieldAccessExpr);

  while (source->GetKind() != AstNodeKind::kNameRefExpr) {
    XLS_CHECK(source->GetKind() == AstNodeKind::kFieldAccessExpr);
    source = ((FieldAccessExpr *)source)->source();
  }

  auto name_ref = (NameRefExpr *)source;
  auto anno = name_ref->annotation();
  XLS_CHECK(anno.has_value());
  return std::make_pair(name_ref->name(), anno->size);
}

absl::Status
FieldAccessElimination::VisitFieldAccessExpr(FieldAccessExpr *expr) {
  XLS_CHECK(expr->annotation().has_value());

  auto anno = expr->annotation();
  auto name = anno->struct_var_name;
  bool is_global = anno->is_global;
  uint32_t range_start = anno->offset;
  uint32_t range_end = anno->offset + anno->size - 1;

  auto [inner_name, inner_size] = GetInnerStructVarAnnotation(expr);
  XLS_CHECK(inner_name == name);

  auto module = expr->module();
  auto var_def = module->AddFakeVarDef(name, inner_size);
  var_def->SetIsGlobal(is_global);
  auto var_ref = module->AddVarRefExpr(var_def);
  auto bit_slice = module->AddBitSliceExpr(var_ref, range_end, range_start);

  bool status = expr->parent()->ReplaceChild(expr, bit_slice);
  XLS_CHECK(status);

  if (lowering_info_) {
    lowering_info_->field_accesses.insert(expr);
    lowering_info_->defs.insert(var_def);
    lowering_info_->field_access_map[expr] = bit_slice;
    lowering_info_->readable_name_map[bit_slice] =
        GenerateName(expr, delimiter_);
  }

  if (mapping_) {
    mapping_->ast2lowering[bit_slice] = expr;
  }
  return absl::OkStatus();
}

absl::Status
FieldAccessEliminationVerify::VisitFieldAccessExpr(FieldAccessExpr *expr) {
  XLS_LOG(WARNING) << "Should not exist this kind of AstNode";
  return absl::FailedPreconditionError(absl::StrCat(
      "Should not exist this kind of AstNodeAfter `FieldAccessElimination`: ",
      expr->ToString(2, 0)));
}

// Collect those Array-Index Expr
struct ArrayIndexLoweringInfo {
  using ArrIndexMap = std::map<ArrIndexExpr *, VarRefExpr *>;

  std::set<ArrIndexExpr *> arr_indexes;
  std::set<FakeVarDef *>
      defs; // generated fake variables' def. for Array Index usage
  ArrIndexMap index_map;
};

// Convert `arr[x]` to a Dummy Variable Reference : `arr_x`
class ArrayAccessElimination : public VisitAll {
public:
  ArrayAccessElimination(ArrayIndexLoweringInfo *lowering_info,
                         const std::string &delimiter,
                         xls::p5::LoweringMappingRel *mapping = nullptr)
      : lowering_info_(lowering_info), delimiter_(delimiter),
        mapping_(mapping) {}

  absl::Status VisitArrIndexExpr(ArrIndexExpr *node) override;

private:
  ArrayIndexLoweringInfo *lowering_info_;
  std::string delimiter_;
  xls::p5::LoweringMappingRel *mapping_;
};

// Verify that there is no `ArrIndexExpr`
class ArrayAccessEliminationVerify : public VisitAll {
public:
  absl::Status VisitArrIndexExpr(ArrIndexExpr *node) override;
};

std::string GetRefName(Expr *expr) {
  XLS_CHECK(expr->GetKind() == AstNodeKind::kNameRefExpr ||
            expr->GetKind() == AstNodeKind::kVarRefExpr);
  if (expr->GetKind() == AstNodeKind::kNameRefExpr) {
    return ((NameRefExpr *)expr)->name();
  } else {
    return ((VarRefExpr *)expr)->def()->name();
  }
}

absl::Status ArrayAccessElimination::VisitArrIndexExpr(ArrIndexExpr *node) {
  auto target = node->expr();

  XLS_CHECK(target->GetKind() == AstNodeKind::kNameRefExpr);
  auto nameref = (NameRefExpr *)target;
  std::string new_name =
      absl::StrCat(GetRefName(target), delimiter_, std::to_string(node->idx()));
  auto module = node->module();
  XLS_CHECK(nameref->annotation().has_value());
  auto var_def = module->AddFakeVarDef(new_name, nameref->annotation()->size);
  var_def->SetIsGlobal(nameref->annotation()->is_global);
  auto var_ref = module->AddVarRefExpr(var_def);
  bool status = node->parent()->ReplaceChild(node, var_ref);
  XLS_CHECK(status);

  if (lowering_info_) {
    lowering_info_->arr_indexes.insert(node);
    lowering_info_->defs.insert(var_def);
    lowering_info_->index_map[node] = var_ref;
  }

  if (mapping_) {
    mapping_->ast2lowering[var_ref] = node;
  }

  return absl::OkStatus();
}

absl::Status
ArrayAccessEliminationVerify::VisitArrIndexExpr(ArrIndexExpr *node) {
  XLS_LOG(WARNING) << "Should not exist this kind of AstNode";

  return absl::FailedPreconditionError(absl::StrCat(
      "Should not exist this kind of AstNodeAfter `ArrayAccessElimination`: ",
      node->ToString(2, 0)));
}

// Collect those variables that need valid bit
struct ValidBitLoweringInfo {
  using ValidCallsiteMap = std::map<BuiltinCallExpr *, VarRefExpr *>;
  using ValidSetCallsiteMap = std::map<ExprEvalStmt *, AssignStmt *>;
  using VarNameRefPtr = absl::variant<NameRefExpr *, VarRefExpr *>;

  std::set<BuiltinCallExpr *> valid_callsites;
  ValidCallsiteMap valid_map;

  std::set<ExprEvalStmt *> valid_set_callsites;
  ValidSetCallsiteMap valid_set_map;

  std::set<VarNameRefPtr> refs; // orginal variables' names
  std::map<VarNameRefPtr, FakeVarDef *>
      ref2bit; // map variable's name reference to its valid-bit defination
  std::set<FakeVarDef *> defs; // generated fake variables' def. for valid bit
};

// Convert `_valid_set(a, true);` to `a = true;` assignment
//   and convert `_valid(a)` to a Dummy Variable Reference : `a_x_valid`
class ValidAndVaildSetElimination : public VisitAll {
public:
  ValidAndVaildSetElimination(ValidBitLoweringInfo *lowering_info,
                              const std::string &delimiter,
                              xls::p5::LoweringMappingRel *mapping = nullptr)
      : lowering_info_(lowering_info), delimiter_(delimiter),
        mapping_(mapping) {}

  absl::Status VisitExprEvalStmt(ExprEvalStmt *node) override;
  absl::Status VisitBuiltinCallExpr(BuiltinCallExpr *node) override;

private:
  ValidBitLoweringInfo *lowering_info_;
  std::string delimiter_;
  xls::p5::LoweringMappingRel *mapping_;
};

// Verify that there is no `_valid` and `_valid_set` function call
class ValidAndVaildSetEliminationVerify : public VisitAll {
public:
  absl::Status VisitBuiltinCallExpr(BuiltinCallExpr *node) override;
};

absl::Status
ValidAndVaildSetElimination::VisitExprEvalStmt(ExprEvalStmt *node) {
  XLS_CHECK(node->expr()->GetKind() == AstNodeKind::kBuiltinCallExpr);
  auto call_expr = (BuiltinCallExpr *)node->expr();

  if (call_expr->callee() == "_valid_set") {
    XLS_CHECK(call_expr->args().size() == 2);

    auto arg0 = call_expr->args().at(0);
    auto arg_kind = arg0->GetKind();
    XLS_CHECK(arg_kind == AstNodeKind::kNameRefExpr ||
              arg_kind == AstNodeKind::kVarRefExpr);

    auto module = node->module();

    FakeVarDef *var_def = nullptr;
    VarRefExpr *var_ref = nullptr;
    if (arg_kind == AstNodeKind::kNameRefExpr) {
      std::string new_name = GetRefName(arg0) + delimiter_ + "valid";
      auto nameref = (NameRefExpr *)arg0;
      var_def =
          module->AddFakeVarDef(new_name, 1U); /* this is a 1-bit valid bit*/
      XLS_CHECK(nameref->annotation().has_value());
      var_def->SetIsGlobal(nameref->annotation()->is_global);

      var_ref = module->AddVarRefExpr(var_def);
    } else {
      XLS_CHECK(arg_kind == AstNodeKind::kVarRefExpr);
      var_ref = ((VarRefExpr *)arg0);
      var_def = var_ref->def();
    }

    auto assign = module->AddAssignStmt(var_ref, call_expr->args().at(1));
    bool status = node->parent()->ReplaceChild(node, assign);

    XLS_CHECK(status);

    if (lowering_info_) {
      lowering_info_->valid_set_callsites.insert(node);
      lowering_info_->valid_set_map[node] = assign;

      absl::variant<NameRefExpr *, VarRefExpr *> ptr;
      if (arg_kind == AstNodeKind::kNameRefExpr) {
        ptr = (NameRefExpr *)arg0;
      } else {
        ptr = (VarRefExpr *)arg0;
      }

      lowering_info_->refs.insert(ptr);
      lowering_info_->ref2bit[ptr] = var_def;
      lowering_info_->defs.insert(var_def);
    }

    if (mapping_) {
      mapping_->ast2lowering[assign] = node;
    }

  } else {
    XLS_RETURN_IF_ERROR(VisitBuiltinCallExpr(call_expr));
  }
  return absl::OkStatus();
}

absl::Status
ValidAndVaildSetElimination::VisitBuiltinCallExpr(BuiltinCallExpr *node) {
  if (node->callee() == "_valid") {
    XLS_CHECK(node->args().size() == 1);

    auto arg0 = node->args().at(0);
    auto arg_kind = arg0->GetKind();
    XLS_CHECK(arg_kind == AstNodeKind::kNameRefExpr ||
              arg_kind == AstNodeKind::kVarRefExpr);
    std::string new_name =
        GetRefName(node->args().at(0)) + delimiter_ + "valid";

    auto module = node->module();
    auto var_def =
        module->AddFakeVarDef(new_name, 1U); /* this is a 1-bit valid bit*/
    if (arg_kind == AstNodeKind::kNameRefExpr) {
      auto nameref = (NameRefExpr *)arg0;
      XLS_CHECK(nameref->annotation().has_value());
      var_def->SetIsGlobal(nameref->annotation()->is_global);
    } else {
      auto old_def = ((VarRefExpr *)arg0)->def();
      var_def->SetIsGlobal(old_def->is_global());
    }

    auto var_ref = module->AddVarRefExpr(var_def);
    bool status = node->parent()->ReplaceChild(node, var_ref);
    XLS_CHECK(status);

    if (lowering_info_) {
      lowering_info_->valid_callsites.insert(node);
      lowering_info_->valid_map[node] = var_ref;

      absl::variant<NameRefExpr *, VarRefExpr *> ptr;
      if (arg_kind == AstNodeKind::kNameRefExpr) {
        ptr = (NameRefExpr *)arg0;
      } else {
        ptr = (VarRefExpr *)arg0;
      }

      lowering_info_->refs.insert(ptr);
      lowering_info_->ref2bit[ptr] = var_def;
      lowering_info_->defs.insert(var_def);
    }

    if (mapping_) {
      mapping_->ast2lowering[var_ref] = node;
    }
  }

  for (auto arg : node->args()) {
    XLS_RETURN_IF_ERROR(VisitExpr(arg));
  }

  return absl::OkStatus();
}

absl::Status
ValidAndVaildSetEliminationVerify::VisitBuiltinCallExpr(BuiltinCallExpr *node) {
  if (node->callee() == "_valid" || node->callee() == "_valid_set") {
    XLS_LOG(WARNING) << "Should not exist this kind of AstNode";

    return absl::FailedPreconditionError(
        absl::StrCat("Should not exist this kind of AstNodeAfter "
                     "`ValidAndVaildSetElimination`: ",
                     node->ToString(2, 0)));
  }

  for (auto arg : node->args()) {
    XLS_RETURN_IF_ERROR(VisitExpr(arg));
  }
  return absl::OkStatus();
}

// Collect those remaining NameRefExpr
struct NameRefLoweringInfo {
  using NameRefMap =
      std::map<NameRefExpr *,
               VarRefExpr *>; // map orignal AST node to lowered AST node

  std::set<NameRefExpr *> name_refs; // orginal NameRefExpr AST Node
  std::set<FakeVarDef *> defs;       // generated fake variables' def
  NameRefMap name_ref_map;
};

// Replace NameRef to VarRef
class NameRefElimation : public VisitAll {
public:
  NameRefElimation(NameRefLoweringInfo *lowering_info,
                   xls::p5::LoweringMappingRel *mapping = nullptr)
      : lowering_info_(lowering_info), mapping_(mapping) {}
  absl::Status VisitNameRefExpr(NameRefExpr *node) override;

private:
  NameRefLoweringInfo *lowering_info_;
  xls::p5::LoweringMappingRel *mapping_;
};

// Verify that there is no `NameRef`
class NameRefElimationVerify : public VisitAll {
public:
  absl::Status VisitNameRefExpr(NameRefExpr *node) override;
};

absl::Status NameRefElimation::VisitNameRefExpr(NameRefExpr *node) {
  auto name = node->name();
  auto anno = node->annotation();
  if (!anno.has_value()) {
    XLS_LOG(WARNING) << "NameRef has no Annotation: " << node->ToString(2, 0);
    XLS_LOG(WARNING) << "Set to global variable, size : 32 ";
    anno = NameRefExpr::Annotation(32, true);
  }
  auto size = anno->size;
  auto is_global = anno->is_global;
  auto module = node->module();

  auto var_def = module->AddFakeVarDef(name, size);
  var_def->SetIsGlobal(is_global);

  auto var_ref = module->AddVarRefExpr(var_def);

  bool status = node->parent()->ReplaceChild(node, var_ref);
  XLS_CHECK(status);

  if (lowering_info_) {
    lowering_info_->name_refs.insert(node);
    lowering_info_->defs.insert(var_def);
    lowering_info_->name_ref_map[node] = var_ref;
  }

  if (mapping_) {
    mapping_->ast2lowering[var_ref] = node;
  }
  return absl::OkStatus();
}

absl::Status NameRefElimationVerify::VisitNameRefExpr(NameRefExpr *node) {
  XLS_LOG(WARNING) << "Should not exist this kind of AstNode";
  return absl::FailedPreconditionError(absl::StrCat(
      "Should not exist this kind of AstNodeAfter `NameRefElimation`: ",
      node->ToString(2, 0)));
}

// Collect those Nested BitSlice Expr
struct NestedSliceLoweringInfo {
  using NestedSliceMap =
      std::map<BitSliceExpr *,
               BitSliceExpr *>; // map orignal AST node to lowered AST node

  std::set<BitSliceExpr *> nested_slices;
  NestedSliceMap nested_slice_map;
};

// Replace `a[59:10][39:20][9:0]` to `a[59:10][29:20]` then to `a[39:30]`
class NestedSliceElimation : public VisitAll {
public:
  NestedSliceElimation(NestedSliceLoweringInfo *lowering_info,
                       xls::p5::LoweringMappingRel *mapping = nullptr)
      : lowering_info_(lowering_info), mapping_(mapping) {}

  // fix point iteration
  absl::Status VisitModule(Module *node) override {
    changed_ = true;
    while (changed_) {
      changed_ = false;
      XLS_RETURN_IF_ERROR(VisitStmtBlock(node->body));
    }
    return absl::OkStatus();
  }

  absl::Status VisitBitSliceExpr(BitSliceExpr *node) override {
    auto target = node->target();
    auto target_kind = target->GetKind();
    auto outer_max_bit = node->max_bit();
    auto outer_min_bit = node->min_bit();

    if (target_kind == AstNodeKind::kBitSliceExpr) {
      changed_ = true; // need iteration

      auto inner_slice = (BitSliceExpr *)target;
      auto inner_max_bit = inner_slice->max_bit();
      auto inner_min_bit = inner_slice->min_bit();

      uint32_t inner_size = (inner_max_bit - inner_min_bit + 1);
      uint32_t outer_size = (outer_max_bit - outer_min_bit + 1);
      XLS_CHECK((outer_size <= inner_size) && (outer_max_bit < inner_size));
      auto module = inner_slice->module();
      auto new_slice = module->AddBitSliceExpr(inner_slice->target(),
                                               inner_min_bit + outer_max_bit,
                                               inner_min_bit + outer_min_bit);

      bool status = node->parent()->ReplaceChild(node, new_slice);
      XLS_CHECK(status);

      if (lowering_info_) {
        lowering_info_->nested_slices.insert(node);
        lowering_info_->nested_slice_map[node] = new_slice;
      }

      if (mapping_) {
        mapping_->ast2lowering[new_slice] = node;
      }

      return absl::OkStatus();
    }
    return VisitExpr(target);
  }

private:
  bool changed_ = false;
  NestedSliceLoweringInfo *lowering_info_;
  xls::p5::LoweringMappingRel *mapping_;
};

// Verify that there is no `NameRef`
class NestedSliceElimationVerify : public VisitAll {
public:
  absl::Status VisitBitSliceExpr(BitSliceExpr *node) override {
    if (node->target()->GetKind() == AstNodeKind::kBitSliceExpr) {
      XLS_LOG(WARNING) << "Should not exist nested BitSliceExpr : "
                       << node->ToString(2, 0);

      return absl::FailedPreconditionError(absl::StrCat(
          "Should not exist nested BitSliceExpr After `NestedSliceElimation`: ",
          node->ToString(2, 0)));
    }
    return absl::OkStatus();
  }
};

struct LoweringInfo {
  FieldAccessLoweringInfo field_access;
  ArrayIndexLoweringInfo arr_idx;
  ValidBitLoweringInfo valid_bit;
  NameRefLoweringInfo name_ref;
  NestedSliceLoweringInfo nested_slice;
};

// Doing different kind of lowering transform to Module
//  BE CAREFUL!
//    Because of the implementation detail, you can't change the
//    sequence of lowering procedures
absl::Status LoweringTransform(LoweringInfo *info, Module *module,
                               const std::string &delimiter, bool need_verify,
                               xls::p5::LoweringMappingRel *mapping) {
  XLS_CHECK(info && module);
  // DEBUG
  // XLS_LOG(INFO) std::cout
  //    << "After removing Useless Block and combining Nested IfStmt: \n"
  //    << module->body->ToString(2, 0);

  FieldAccessElimination lowering1(&(info->field_access), delimiter, mapping);
  XLS_RETURN_IF_ERROR(lowering1.VisitModule(module));

  ArrayAccessElimination lowering2(&(info->arr_idx), delimiter, mapping);
  XLS_RETURN_IF_ERROR(lowering2.VisitModule(module));

  ValidAndVaildSetElimination lowering3(&(info->valid_bit), delimiter, mapping);
  XLS_RETURN_IF_ERROR(lowering3.VisitModule(module));

  NameRefElimation lowering4(&(info->name_ref), mapping);
  XLS_RETURN_IF_ERROR(lowering4.VisitModule(module));

  UselessBlockElimination useless_block_eliminator;
  XLS_RETURN_IF_ERROR(useless_block_eliminator.VisitModule(module));

  NestedIfStmtElimination nested_if_combiner;
  XLS_RETURN_IF_ERROR(nested_if_combiner.VisitModule(module));

  NestedSliceElimation nested_slice_eliminator(&(info->nested_slice), mapping);
  XLS_RETURN_IF_ERROR(nested_slice_eliminator.VisitModule(module));

  if (need_verify) {
    FieldAccessEliminationVerify verify1;
    XLS_RETURN_IF_ERROR(verify1.VisitModule(module));
    ArrayAccessEliminationVerify verify2;
    XLS_RETURN_IF_ERROR(verify2.VisitModule(module));
    ValidAndVaildSetEliminationVerify verify3;
    XLS_RETURN_IF_ERROR(verify3.VisitModule(module));
    NameRefElimationVerify verify4;
    XLS_RETURN_IF_ERROR(verify4.VisitModule(module));
    
    UselessBlockEliminationVerify verfy_no_useless_block;
    XLS_RETURN_IF_ERROR(verfy_no_useless_block.VisitModule(module));
    NestedIfStmtEliminationVerify verify_no_nested_if;
    XLS_RETURN_IF_ERROR(verify_no_nested_if.VisitModule(module));
    NestedSliceElimationVerify verify_no_slice;
    XLS_RETURN_IF_ERROR(verify_no_slice.VisitModule(module));

    auto pred_def_has_size = [](FakeVarDef *def) -> bool {
      return def->size().has_value();
    };

    auto pred_defs_all_have_size =
        [pred_def_has_size](std::set<FakeVarDef *> *defs) -> bool {
      return std::all_of(defs->begin(), defs->end(), pred_def_has_size);
    };

    std::vector<std::set<FakeVarDef *> *> def_set_list{
        &(info->field_access.defs), &(info->arr_idx.defs),
        &(info->valid_bit.defs), &(info->name_ref.defs)};

    bool all_has_size = std::all_of(def_set_list.begin(), def_set_list.end(),
                                    pred_defs_all_have_size);

    if (!all_has_size) {
      return absl::AbortedError("Not all variables' size are known");
    }
  }
  return absl::OkStatus();
}

// Collect all live-variables together
// pre-condition: must lowering before analysis
class LiveVarCollection : public AstAnalysisVisitor<std::set<FakeVarDef *>> {
public:
  LiveVarCollection(std::set<FakeVarDef *> *var_collections)
      : AstAnalysisVisitor<std::set<FakeVarDef *>>(var_collections) {}

  absl::Status VisitVarRefExpr(VarRefExpr *node) override {
    result_->insert(node->def());
    return absl::OkStatus();
  }
};

// Currently, I don't have any type information,
// so, what this class does, is just a very weak version of type inference
//  need lowering
class VarSizeInference : public VisitAll {
public:
  absl::Status VisitAssignStmt(AssignStmt *node) override;
  absl::Status VisitBitSliceExpr(BitSliceExpr *node) override;

  absl::Status Run(StmtBlock *func_body) { return VisitStmtBlock(func_body); }
};

absl::Status VarSizeInference::VisitAssignStmt(AssignStmt *node) {
  XLS_RETURN_IF_ERROR(VisitLvalue(node->lhs()));
  XLS_RETURN_IF_ERROR(VisitExpr(node->rhs()));

  if (node->lhs()->GetKind() == AstNodeKind::kVarRefExpr) {
    auto var_ref = (VarRefExpr *)node->lhs();
    auto var_def = var_ref->def();

    if (node->rhs()->GetKind() == AstNodeKind::kLongIntLiteralExpr) {
      auto long_literal = (LongIntLiteralExpr *)node->rhs();
      int64_t size = long_literal->value().size() * 64;
      var_def->TryUpdateSize(size);
    }
  }
  return absl::OkStatus();
}

absl::Status VarSizeInference::VisitBitSliceExpr(BitSliceExpr *node) {
  XLS_RETURN_IF_ERROR(VisitExpr(node->target()));

  auto target = node->target();
  XLS_CHECK(target->GetKind() == AstNodeKind::kVarRefExpr);
  auto var_ref = (VarRefExpr *)target;
  auto var_def = var_ref->def();

  if (node->max_bit() + 1 <= 0) {
    XLS_LOG(WARNING) << "Bitslice length is wrong, content : "
                     << node->ToString(2, 0);
    return absl::InvalidArgumentError(absl::StrCat(
        "Bitslice length is wrong, content : ", node->ToString(2, 0)));
  }

  var_def->TryUpdateSize(node->max_bit() + 1);
  return absl::OkStatus();
}

// Analysis the return stmt's hit condition
//  donnot need lowering
class ExitPredictionAnalysis
    : public AstAnalysisVisitor<std::vector<std::pair<ReturnStmt *, Expr *>>> {
public:
  ExitPredictionAnalysis(std::vector<std::pair<ReturnStmt *, Expr *>> *exits)
      : AstAnalysisVisitor<std::vector<std::pair<ReturnStmt *, Expr *>>>(
            exits) {}

  absl::Status VisitStmtBlock(StmtBlock *node) override;
  absl::Status VisitIfStmt(IfStmt *node) override;
  absl::Status VisitIfElseStmt(IfElseStmt *node) override;
  absl::Status VisitReturnStmt(ReturnStmt *node) override;

private:
  std::map<Stmt *, Expr *> hit_condition_;
};

absl::Status ExitPredictionAnalysis::VisitStmtBlock(StmtBlock *node) {
  if (hit_condition_.find(node) !=
      hit_condition_.end()) { // hit_condition_.contains(node)
    Expr *condition = hit_condition_.at(node);
    for (auto stmt : node->stmts()) {
      hit_condition_[stmt] = condition;
    }
  }

  for (auto stmt : node->stmts()) {
    XLS_RETURN_IF_ERROR(VisitStmt(stmt));
  }
  return absl::OkStatus();
}

absl::Status ExitPredictionAnalysis::VisitIfStmt(IfStmt *node) {
  auto condition = node->condition();
  XLS_RETURN_IF_ERROR(VisitExpr(condition));

  if (hit_condition_.find(node) !=
      hit_condition_.end()) { // hit_condition_.contains(node)
    auto module = node->module();
    auto predict = hit_condition_.at(node);
    auto conjunction =
        module->AddBinaryOpExpr(OpKind::kLogicalAnd, predict, condition);
    hit_condition_[node->then_block()] = conjunction;
  } else {
    hit_condition_[node->then_block()] = condition;
  }

  XLS_RETURN_IF_ERROR(VisitStmt(node->then_block()));

  return absl::OkStatus();
}

absl::Status ExitPredictionAnalysis::VisitIfElseStmt(IfElseStmt *node) {
  auto condition = node->condition();
  XLS_RETURN_IF_ERROR(VisitExpr(condition));

  auto module = node->module();
  auto condition_neg = module->AddUnaryOpExpr(OpKind::kLogicalNot, condition);

  if (hit_condition_.find(node) !=
      hit_condition_.end()) { // hit_condition_.contains(node)
    auto predict = hit_condition_.at(node);
    auto conjunction =
        module->AddBinaryOpExpr(OpKind::kLogicalAnd, predict, condition);
    auto conjunction_neg =
        module->AddBinaryOpExpr(OpKind::kLogicalAnd, predict, condition_neg);

    hit_condition_[node->then_block()] = conjunction;
    hit_condition_[node->else_block()] = conjunction_neg;
  } else {
    hit_condition_[node->then_block()] = condition;
    hit_condition_[node->else_block()] = condition_neg;
  }

  XLS_RETURN_IF_ERROR(VisitStmt(node->then_block()));
  XLS_RETURN_IF_ERROR(VisitStmt(node->else_block()));
  return absl::OkStatus();
}

absl::Status ExitPredictionAnalysis::VisitReturnStmt(ReturnStmt *node) {
  if (hit_condition_.find(node) ==
      hit_condition_.end()) { // !hit_condition_.contains(node)
    XLS_LOG(FATAL) << "The Return Stmt\"s predition is not computed";
    return absl::FailedPreconditionError(
        "The Return Stmt\"s predition is not computed");
  }
  result_->emplace_back(node, hit_condition_.at(node));
  return absl::OkStatus();
}

// Analyze whose variables that each stmt may modify
class StmtModifiedVarAnalysis
    : public AstAnalysisVisitor<std::map<Stmt *, std::set<FakeVarDef *>>> {
public:
  explicit StmtModifiedVarAnalysis(
      std::map<Stmt *, std::set<FakeVarDef *>> *stmt_modifies)
      : AstAnalysisVisitor<std::map<Stmt *, std::set<FakeVarDef *>>>(
            stmt_modifies) {}

  absl::Status VisitStmtBlock(StmtBlock *node) override {
    for (auto stmt : node->stmts()) {
      XLS_RETURN_IF_ERROR(VisitStmt(stmt));

      for (auto var : result_->operator[](stmt))
        result_->operator[](node).insert(var);
    }
    return absl::OkStatus();
  }

  absl::Status VisitIfStmt(IfStmt *node) override {
    XLS_RETURN_IF_ERROR(VisitStmt(node->then_block()));
    for (auto var : result_->operator[](node->then_block()))
      result_->operator[](node).insert(var);
    return absl::OkStatus();
  }

  absl::Status VisitIfElseStmt(IfElseStmt *node) override {
    XLS_RETURN_IF_ERROR(VisitStmt(node->then_block()));
    XLS_RETURN_IF_ERROR(VisitStmt(node->else_block()));

    for (auto var : result_->operator[](node->then_block()))
      result_->operator[](node).insert(var);

    for (auto var : result_->operator[](node->else_block()))
      result_->operator[](node).insert(var);
    return absl::OkStatus();
  }

  absl::Status VisitAssignStmt(AssignStmt *node) override {
    auto lhs = node->lhs();
    switch (lhs->GetKind()) {
    case AstNodeKind::kVarRefExpr:
      result_->operator[](node).insert(((VarRefExpr *)lhs)->def());
      break;
    case AstNodeKind::kBitSliceExpr: {
      auto target = ((BitSliceExpr *)lhs)->target();
      XLS_CHECK(target->GetKind() == AstNodeKind::kVarRefExpr);
      // XLS_LOG_IF(FATAL, target->GetKind() != AstNodeKind::kVarRefExpr) <<
      // node->ToString(2, 0);
      result_->operator[](node).insert(((VarRefExpr *)target)->def());
    } break;
    default:
      XLS_LOG(FATAL) << "unsupported type" << std::endl;
    }
    return absl::OkStatus();
  }
};

/*
void AllocateSizeForCurrentlyUnknown(const std::set<FakeVarDef *> &variables,
                                     uint64_t size) {
  for (auto def : variables) {
    XLS_CHECK(def && "pointer not empty");
    if (!def->size().has_value()) {
      def->TryUpdateSize(size);
    }
  }
}
*/

using namespace boost::icl;
using LiveRange = std::pair<uint32_t, uint32_t>;
using LiveRanges = std::vector<LiveRange>;
using LiveRangesMapPtrPair = std::pair<std::map<FakeVarDef *, LiveRanges> *,
                                       std::map<FakeVarDef *, LiveRanges> *>;

class VarLiveRangesAnalysis : public AstAnalysisVisitor<LiveRangesMapPtrPair> {
private:
  std::map<FakeVarDef *, split_interval_set<uint32_t>> fine_grained_ranges_;
  std::map<FakeVarDef *, interval_set<uint32_t>> coarse_grained_ranges_;

public:
  VarLiveRangesAnalysis(LiveRangesMapPtrPair *map_ptr_pair)
      : AstAnalysisVisitor<LiveRangesMapPtrPair>(map_ptr_pair) {}

  absl::Status Run(StmtBlock *func_body) override {
    XLS_CHECK_OK(VisitStmtBlock(func_body));

    for (auto &[def, _] : fine_grained_ranges_) {
      if (def->is_global()) {
        XLS_LOG(INFO) << "Global var: " << def->name();
      } else {
        XLS_LOG(INFO) << "Local var: " << def->name();
      }
      XLS_LOG(INFO) << def->name() << "'s fine grained live ranges: ";
      auto &ranges1 = fine_grained_ranges_[def];
      for (auto it = ranges1.begin(); it != ranges1.end(); ++it) {
        auto lower = it->lower();
        auto upper = it->upper();

        if (lower < upper) {
          XLS_LOG(INFO) << absl::StrFormat("[%u, %u]", lower, upper - 1);
          (*(GetAnlysisResult()->first))[def].push_back(
              std::pair<uint32_t, uint32_t>(lower, upper - 1));
        }
      }

      XLS_LOG(INFO) << def->name() << "'s corase grained live ranges: ";
      auto &ranges2 = coarse_grained_ranges_[def];
      for (auto it = ranges2.begin(); it != ranges2.end(); ++it) {
        auto lower = it->lower();
        auto upper = it->upper();

        if (lower < upper) {
          XLS_LOG(INFO) << absl::StrFormat("[%u, %u]", lower, upper - 1);
          (*(GetAnlysisResult()->second))[def].push_back(
              std::pair<uint32_t, uint32_t>(lower, upper - 1));
        }
      }
    }

    return absl::OkStatus();
  }

  absl::Status VisitVarRefExpr(VarRefExpr *node) override {
    auto def = node->def();
    XLS_CHECK(def->size().has_value());
    auto size = def->size().value();
    XLS_LOG_IF(FATAL, size == 0)
        << "Variable's size should not be 0, content: " << node->ToString(2, 0);

    fine_grained_ranges_[def].insert(
        discrete_interval<uint32_t>::right_open(0, size));
    coarse_grained_ranges_[def].insert(
        discrete_interval<uint32_t>::right_open(0, size));

    return absl::OkStatus();
  }

  absl::Status VisitBitSliceExpr(BitSliceExpr *node) override {
    auto target = node->target();
    XLS_CHECK(target->GetKind() == AstNodeKind::kVarRefExpr);

    auto def = ((VarRefExpr *)target)->def();

    auto min_bit = node->min_bit();
    auto max_bit = node->max_bit();
    XLS_CHECK(max_bit >= min_bit && min_bit >= 0);
    fine_grained_ranges_[def].insert(
        discrete_interval<uint32_t>::right_open(min_bit, max_bit + 1));

    coarse_grained_ranges_[def].insert(
        discrete_interval<uint32_t>::right_open(min_bit, max_bit + 1));

    return absl::OkStatus();
  }
};

// All analysis information that are used for translation
//   - all global/local variables are gathered together in `variables`
//   - all return-stmts' hint conditions are predicted in `exits_predict_expr`
//   - the variables that each stmt may modify are saved in `stmt_modified_vars`
//   - all variable's size are known, i.e., FakeVarDef's size should not be
//      absl::nullopt
//      note that currently they are inferenced or generated to a fix size = 64
//      bit
struct AstAnalysisInformation {
  explicit AstAnalysisInformation(StmtBlock *func_body) {
    // collect all live variables
    LiveVarCollection analysis1(&variables);
    auto status = analysis1.Run(func_body);

    // filter those global variables
    std::copy_if(variables.begin(), variables.end(),
                 std::back_inserter(global_vars),
                 [](FakeVarDef *def) { return def->is_global(); });

    // filter those local variables
    std::copy_if(variables.begin(), variables.end(),
                 std::back_inserter(local_vars),
                 [](FakeVarDef *def) { return !def->is_global(); });

    XLS_LOG(INFO) << "Global Variables count : " << global_vars.size();
    XLS_LOG(INFO) << "Local Variables count : " << local_vars.size();
    for (auto def : variables) {
      XLS_LOG(INFO) << def->name() << ", is global : " << def->is_global()
                    << ", size : " << def->size().value();
    }

    auto args = std::make_pair(&fine_graned_ranges, &coarse_graned_ranges);
    VarLiveRangesAnalysis live_range_analysis(&args);
    status.Update(live_range_analysis.Run(func_body));

    ExitPredictionAnalysis analysis2(&exits_predict_expr);
    status.Update(analysis2.Run(func_body));

    StmtModifiedVarAnalysis analysis3(&stmt_modified_vars);
    status.Update(analysis3.Run(func_body));

    VarSizeInference analysis4;
    status.Update(analysis4.Run(func_body));
    // AllocateSizeForCurrentlyUnknown(variables, 64);

    XLS_LOG_IF(FATAL, !status.ok()) << status;
  }

  std::set<FakeVarDef *> variables;
  std::vector<FakeVarDef *> global_vars;
  std::vector<FakeVarDef *> local_vars;

  // combine fine-grained live ranges, can get the coarse-grained live ranges
  std::map<FakeVarDef *, LiveRanges>
      fine_graned_ranges; // fine-grained live range
  std::map<FakeVarDef *, LiveRanges>
      coarse_graned_ranges; // coarse-grained live range

  std::vector<std::pair<ReturnStmt *, Expr *>> exits_predict_expr;
  std::map<Stmt *, std::set<FakeVarDef *>> stmt_modified_vars;
};

// description global context, i.e., the value of the global variables
using ContextType = std::vector<BValue>;

class P5ActionIrConverter {
public:
  // TODO
  P5ActionIrConverter(StmtBlock *func_body, const std::string &func_name,
                      Package *package, const AstAnalysisInformation &analysis,
                      LoweringMappingRel *mapping_ = nullptr,
                      bool should_verify = true);
  // TODO
  absl::StatusOr<Function *> Build();

private:
  ContextType VisitStmtBlock(StmtBlock *node, const ContextType &ctx);

  ContextType VisitStmt(Stmt *node, const ContextType &ctx);

  ContextType VisitAssignStmt(AssignStmt *node, const ContextType &ctx);

  ContextType VisitIfStmt(IfStmt *node, const ContextType &ctx);

  ContextType VisitIfElseStmt(IfElseStmt *node, const ContextType &ctx);

  // TODO: handle builtin function call: get_anchor, _push_stack_h,
  // _push_stack_b
  ContextType VisitExprEvalStmt(ExprEvalStmt *stmt, const ContextType &ctx);

  // TODO: handle control flow
  ContextType VisitReturnStmt(ReturnStmt *stmt, const ContextType &ctx);

  // TODO: may add latency
  ContextType VisitNopStmt(NopStmt *stmt, const ContextType &ctx);

  BValue VisitExpr(Expr *node, const ContextType &ctx,
                   absl::optional<absl::string_view> name = absl::nullopt);

  BValue
  VisitNameRefExpr(NameRefExpr *node, const ContextType &ctx,
                   absl::optional<absl::string_view> name = absl::nullopt);

  BValue
  VisitVarRefExpr(VarRefExpr *node, const ContextType &ctx,
                  absl::optional<absl::string_view> name = absl::nullopt);

  BValue
  VisitBitSliceExpr(BitSliceExpr *node, const ContextType &ctx,
                    absl::optional<absl::string_view> name = absl::nullopt);

  BValue VisitCastExpr(CastExpr *node, const ContextType &ctx,
                       absl::optional<absl::string_view> name = absl::nullopt);

  BValue
  VisitBinaryOpExpr(BinaryOpExpr *node, const ContextType &ctx,
                    absl::optional<absl::string_view> name = absl::nullopt);

  BValue
  VisitUnaryOpExpr(UnaryOpExpr *node, const ContextType &ctx,
                   absl::optional<absl::string_view> name = absl::nullopt);

  BValue VisitLongIntLiteralExpr(
      LongIntLiteralExpr *node, const ContextType &ctx,
      absl::optional<absl::string_view> name = absl::nullopt);

  BValue
  VisitBuiltinCallExpr(BuiltinCallExpr *node, const ContextType &ctx,
                       absl::optional<absl::string_view> name = absl::nullopt);

  BValue
  VisitIntLiteralExpr(IntLiteralExpr *node, const ContextType &ctx,
                      absl::optional<absl::string_view> name = absl::nullopt);

private:
  size_t DefToIdx(FakeVarDef *def) const;
  size_t DefToSize(FakeVarDef *def) const;
  std::string Idx2Name(size_t idx) const;
  BValue ChangeSize(BValue value, size_t new_size,
                    absl::optional<absl::string_view> name = absl::nullopt);

  StmtBlock *action_body_;
  LoweringMappingRel *mapping_;
  const AstAnalysisInformation &analysis_;

  FunctionBuilder builder_;

  ContextType input_ctx_;
  std::map<FakeVarDef *, size_t> def2idx_;
  std::vector<FakeVarDef *> idx2def_;

  std::map<Expr *, BValue> expr_value_;        // handle by VisitExpr
  std::map<Stmt *, ContextType> stmt_in_ctx_;  // handle by VisitStmt
  std::map<Stmt *, ContextType> stmt_out_ctx_; // handle by VisitStmt
};

bool operator==(const BValue &v1, const BValue &v2) {
  return v1.node() == v2.node() && v1.builder() == v2.builder();
}

bool operator!=(const BValue &v1, const BValue &v2) { return !(v1 == v2); }

BValue P5ActionIrConverter::ChangeSize(BValue value, size_t new_size,
                                       absl::optional<absl::string_view> name) {
  auto old_size = value.BitCountOrDie();
  if (old_size == new_size)
    return value;

  if (old_size < new_size)
    return builder_.ZeroExtend(value, new_size, SourceInfo(),
                               name.value_or(""));
  return builder_.BitSlice(value, 0, new_size, SourceInfo(), name.value_or(""));
}

P5ActionIrConverter::P5ActionIrConverter(StmtBlock *func_body,
                                         const std::string &func_name,
                                         Package *package,
                                         const AstAnalysisInformation &analysis,
                                         LoweringMappingRel *mapping_,
                                         bool should_verify)
    : action_body_(func_body), mapping_(mapping_), analysis_(analysis),
      builder_(func_name, package, should_verify) {

  for (auto def : analysis_.variables) {
    XLS_CHECK(def->size().has_value());
    auto size = def->size().value(); // variable's bitcount
    def2idx_[def] = input_ctx_.size();
    idx2def_.push_back(def);

    input_ctx_.emplace_back(
        builder_.Param(def->name(), package->GetBitsType(size)));
  }
}

absl::StatusOr<Function *> P5ActionIrConverter::Build() {
  ContextType ctx_without_return = VisitStmtBlock(action_body_, input_ctx_);

  std::vector<uint32_t> global_var_idx;
  global_var_idx.reserve(analysis_.global_vars.size());
  for (auto def : analysis_.global_vars) {
    global_var_idx.push_back(DefToIdx(def));
  }

  // the total number of exits
  size_t exit_no = analysis_.exits_predict_expr.size();
  // the total number of global variables
  size_t var_no = global_var_idx.size();

  // [var_no, exit_no] dimension matrix
  std::vector<std::vector<BValue>> final_merge_value;
  final_merge_value.resize(var_no);
  for (auto &final_value : final_merge_value) {
    final_value.reserve(exit_no);
  }

  std::vector<BValue> exit_predict_bits;
  for (auto [ret_stmt, predict_expr] : analysis_.exits_predict_expr) {
    XLS_CHECK(ret_stmt->GetKind() == AstNodeKind::kReturnStmt &&
              predict_expr->IsExprKind());

    ContextType ctx = stmt_in_ctx_.at(ret_stmt);
    exit_predict_bits.emplace_back(VisitExpr(predict_expr, ctx));
    XLS_CHECK(exit_predict_bits.back().BitCountOrDie() == 1);

    const auto &exit_ctx = stmt_out_ctx_.at(ret_stmt);

    for (int i = 0; i < var_no; ++i) {
      final_merge_value[i].push_back(exit_ctx.at(global_var_idx.at(i)));
    }
  }

  for (int i = 0; i < var_no; ++i) {
    final_merge_value[i].push_back(ctx_without_return.at(global_var_idx.at(i)));
  }

  BValue predict = builder_.Concat(exit_predict_bits, SourceInfo(), "predict");
  BValue control_encoding =
      builder_.OneHot(predict, LsbOrMsb::kMsb, SourceInfo(), "predict_one_hot");

  ContextType output_ctx;
  output_ctx.reserve(analysis_.global_vars.size());

  for (int i = 0; i < var_no; ++i) {
    output_ctx.push_back(builder_.OneHotSelect(
        control_encoding, final_merge_value[i], SourceInfo(),
        absl::StrFormat("final_%s", Idx2Name(global_var_idx.at(i)))));
  }

  BValue body = builder_.Tuple(output_ctx, SourceInfo(), "body");

  return builder_.BuildWithReturnValue(body);
}

size_t P5ActionIrConverter::DefToIdx(FakeVarDef *def) const {
  XLS_CHECK(def);
  return def2idx_.at(def);
}

size_t P5ActionIrConverter::DefToSize(FakeVarDef *def) const {
  XLS_CHECK(def && def->size().has_value());
  return def->size().value();
}

std::string P5ActionIrConverter::Idx2Name(size_t idx) const {
  return idx2def_.at(idx)->name();
}

ContextType P5ActionIrConverter::VisitStmt(Stmt *stmt, const ContextType &ctx) {
  ContextType result;

  stmt_in_ctx_[stmt] = ctx;

  switch (stmt->GetKind()) {
  case AstNodeKind::kIfStmt:
    result = VisitIfStmt((IfStmt *)stmt, ctx);
    break;
  case AstNodeKind::kIfElseStmt:
    result = VisitIfElseStmt((IfElseStmt *)stmt, ctx);
    break;
  case AstNodeKind::kAssignStmt:
    result = VisitAssignStmt((AssignStmt *)stmt, ctx);
    break;
  case AstNodeKind::kStmtBlock:
    result = VisitStmtBlock((StmtBlock *)stmt, ctx);
    break;
  case AstNodeKind::kExprEvalStmt:
    result = VisitExprEvalStmt((ExprEvalStmt *)stmt, ctx);
    break;
  case AstNodeKind::kReturnStmt:
    result = VisitReturnStmt((ReturnStmt *)stmt, ctx);
    break;
  case AstNodeKind::kNopStmt:
    result = VisitNopStmt((NopStmt *)stmt, ctx);
    break;
  default:
    XLS_LOG(FATAL) << "unsupported Stmt type in StmtBlock, AstNode content : "
                   << stmt->ToString(2, 0) << std::endl;
    break;
  }

  stmt_out_ctx_[stmt] = result;

  XLS_CHECK(result.size() == ctx.size());
  return result;
}

ContextType P5ActionIrConverter::VisitStmtBlock(StmtBlock *node,
                                                const ContextType &ctx) {
  ContextType result(ctx); // current context is cached
  for (auto stmt : node->stmts()) {
    result = VisitStmt(stmt, result);
  }
  return result;
}

ContextType P5ActionIrConverter::VisitIfStmt(IfStmt *node,
                                             const ContextType &ctx) {
  BValue selector = VisitExpr(node->condition(), ctx, "if_cond");
  ContextType then_clause_ctx = VisitStmt(node->then_block(), ctx);

  ContextType result(ctx);
  for (auto var : analysis_.stmt_modified_vars.at(node)) {
    auto idx = DefToIdx(var);

    if (then_clause_ctx.at(idx) != ctx.at(idx)) {
      result[idx] = builder_.Select(
          selector, then_clause_ctx[idx], ctx.at(idx), SourceInfo(),
          absl::StrFormat("sel_%s_%s", var->name(), selector.GetName()));
    }
  }

  return result;
}

ContextType P5ActionIrConverter::VisitIfElseStmt(IfElseStmt *node,
                                                 const ContextType &ctx) {
  BValue selector = VisitExpr(node->condition(), ctx, "if_else_cond");
  ContextType then_clause_ctx = VisitStmt(node->then_block(), ctx);
  ContextType else_clause_ctx = VisitStmt(node->else_block(), ctx);

  ContextType result(ctx);
  for (auto var : analysis_.stmt_modified_vars.at(node)) {
    auto idx = DefToIdx(var);

    if (then_clause_ctx.at(idx) != ctx.at(idx) ||
        else_clause_ctx.at(idx) != ctx.at(idx)) {
      result[idx] = builder_.Select(
          selector, then_clause_ctx[idx], else_clause_ctx[idx], SourceInfo(),
          absl::StrFormat("sel_%s_%s", var->name(), selector.GetName()));
    }
  }

  return result;
}

ContextType P5ActionIrConverter::VisitAssignStmt(AssignStmt *node,
                                                 const ContextType &ctx) {
  ContextType result(ctx);
  BValue rhs_value = VisitExpr(node->rhs(), ctx);

  auto lhs = node->lhs();
  XLS_CHECK(lhs->GetKind() == AstNodeKind::kVarRefExpr ||
            lhs->GetKind() == AstNodeKind::kBitSliceExpr);

  if (lhs->GetKind() == AstNodeKind::kVarRefExpr) {
    auto var_def = ((VarRefExpr *)lhs)->def();
    size_t idx = DefToIdx(var_def);

    result[idx] = ChangeSize(rhs_value, DefToSize(var_def), var_def->name());
  } else { // BitSlice
    XLS_CHECK(lhs->GetKind() == AstNodeKind::kBitSliceExpr);
    auto slice = (BitSliceExpr *)lhs;

    XLS_CHECK(slice->target()->GetKind() == AstNodeKind::kVarRefExpr);
    auto var_def = ((VarRefExpr *)(slice->target()))->def();
    size_t idx = DefToIdx(var_def);
    auto slice_size = slice->max_bit() - slice->min_bit() + 1;

    BValue rhs_value_resized = ChangeSize(rhs_value, slice_size);

    result[idx] = builder_.BitSliceUpdate(
        result[idx],
        builder_.Literal(UBits(slice->min_bit(), 64), SourceInfo(),
                         absl::StrFormat("const_%d", slice->min_bit())),
        rhs_value_resized, SourceInfo(),
        absl::StrFormat("%s_slice_update", var_def->name()));
  }
  return result;
}

// TODO: handle builtin function call: get_anchor, _push_stack_h,
// _push_stack_b
ContextType P5ActionIrConverter::VisitExprEvalStmt(ExprEvalStmt *stmt,
                                                   const ContextType &ctx) {
  XLS_CHECK(stmt->expr()->GetKind() == AstNodeKind::kBuiltinCallExpr);
  auto call_expr = (BuiltinCallExpr *)stmt->expr();
  // BValue call_expr_value = VisitBuiltinCallExpr(call_expr, ctx);

  XLS_CHECK(call_expr->callee() == "_get_anchor" ||
            call_expr->callee() == "_stack_push_h" ||
            call_expr->callee() == "_stack_push_b");
  // TODO: handle builtin function call: get_anchor, _push_stack_h,
  // _push_stack_b
  return ctx;
}

ContextType P5ActionIrConverter::VisitReturnStmt(ReturnStmt *stmt,
                                                 const ContextType &ctx) {
  return ctx;
}

// TODO: may add latency
ContextType P5ActionIrConverter::VisitNopStmt(NopStmt *stmt,
                                              const ContextType &ctx) {
  return ctx;
}

BValue P5ActionIrConverter::VisitExpr(Expr *node, const ContextType &ctx,
                                      absl::optional<absl::string_view> name) {
  BValue result;
  if (expr_value_.find(node) != expr_value_.end()) {
    return expr_value_.at(node);
  }

  switch (node->GetKind()) {
  case AstNodeKind::kBitSliceExpr:
    result = VisitBitSliceExpr((BitSliceExpr *)node, ctx, name);
    break;
  case AstNodeKind::kNameRefExpr:
    result = VisitNameRefExpr((NameRefExpr *)node, ctx, name);
    break;
  case AstNodeKind::kVarRefExpr:
    result = VisitVarRefExpr((VarRefExpr *)node, ctx, name);
    break;
  case AstNodeKind::kCastExpr:
    result = VisitCastExpr((CastExpr *)node, ctx, name);
    break;
  case AstNodeKind::kUnaryOpExpr:
    result = VisitUnaryOpExpr((UnaryOpExpr *)node, ctx, name);
    break;
  case AstNodeKind::kBinaryOpExpr:
    result = VisitBinaryOpExpr((BinaryOpExpr *)node, ctx, name);
    break;
  case AstNodeKind::kBuiltinCallExpr:
    result = VisitBuiltinCallExpr((BuiltinCallExpr *)node, ctx, name);
    break;
  case AstNodeKind::kIntLiteralExpr:
    result = VisitIntLiteralExpr((IntLiteralExpr *)node, ctx, name);
    break;
  case AstNodeKind::kLongIntLiteralExpr:
    result = VisitLongIntLiteralExpr((LongIntLiteralExpr *)node, ctx, name);
    break;
  case AstNodeKind::kFieldAccessExpr:
  case AstNodeKind::kArrIndexExpr:
  default: {
    XLS_LOG(FATAL) << "there is not supposed to have this kind of Expr "
                      "Type, content : "
                   << node->ToString(2, 0)
                   << ", parent content : " << node->parent()->ToString(2, 0)
                   << std::endl;
    break;
  }
  }
  expr_value_[node] = result;
  if (mapping_) {
    mapping_->node2ast.insert(
        std::pair<Node *, AstNode *>(result.node(), node));
  }
  return result;
}

BValue P5ActionIrConverter::VisitLongIntLiteralExpr(
    LongIntLiteralExpr *node, const ContextType &ctx,
    absl::optional<absl::string_view> name) {
  auto size = node->value().size();
  absl::InlinedVector<bool, 64> bits(size, 0);
  auto value =
      builder_.Literal(Bits(bits), SourceInfo(), name.value_or("long_literal"));
  return value;
}

BValue
P5ActionIrConverter::VisitNameRefExpr(NameRefExpr *node, const ContextType &ctx,
                                      absl::optional<absl::string_view> name) {
  XLS_CHECK("Should not exist NameRefExpr");
  return BValue();
}

BValue P5ActionIrConverter::VisitIntLiteralExpr(
    IntLiteralExpr *node, const ContextType &ctx,
    absl::optional<absl::string_view> name) {
  auto literal = node->value();

  auto value = builder_.Literal(
      UBits(literal, node->size()), SourceInfo(),
      node->name().value_or(absl::StrFormat("const_%d", literal)));
  return value;
}

BValue
P5ActionIrConverter::VisitVarRefExpr(VarRefExpr *node, const ContextType &ctx,
                                     absl::optional<absl::string_view> name) {
  return ctx.at(DefToIdx(node->def()));
}

BValue
P5ActionIrConverter::VisitBitSliceExpr(BitSliceExpr *node,
                                       const ContextType &ctx,
                                       absl::optional<absl::string_view> name) {
  auto target = node->target();
  BValue target_value = VisitExpr(target, ctx, name);
  BValue value = builder_.BitSlice(target_value, node->min_bit(),
                                   node->max_bit() - node->min_bit() + 1,
                                   SourceInfo(), name.value_or(""));
  return value;
}

BValue
P5ActionIrConverter::VisitCastExpr(CastExpr *node, const ContextType &ctx,
                                   absl::optional<absl::string_view> name) {

  auto expr_value = VisitExpr(node->expr_to_cast(), ctx, name);
  return ChangeSize(expr_value, node->cast_to()->size(),
                    absl::StrCat("cast_", node->cast_to()->name()));
}

BValue
P5ActionIrConverter::VisitBinaryOpExpr(BinaryOpExpr *node,
                                       const ContextType &ctx,
                                       absl::optional<absl::string_view> name) {
  if (node->GetOpKind() == OpKind::kLeftShift ||
      node->GetOpKind() == OpKind::kRightShift) {
    BValue lhs = ChangeSize(VisitExpr(node->lhs(), ctx, name), 64);
    BValue rhs = VisitExpr(node->rhs(), ctx, name);

    BValue result;
    if (node->GetOpKind() == OpKind::kLeftShift) {
      result = builder_.Shll(lhs, rhs, SourceInfo(), name.value_or(""));
    } else {
      result = builder_.Shrl(lhs, rhs, SourceInfo(), name.value_or(""));
    }
    return result;
  }

  if (node->GetOpKind() == OpKind::kLogicalAnd ||
      node->GetOpKind() == OpKind::kLogicalOr) {
    BValue value1 = VisitExpr(node->lhs(), ctx, name);
    BValue value2 = VisitExpr(node->rhs(), ctx, name);
    auto sz1 = value1.BitCountOrDie();
    auto sz2 = value2.BitCountOrDie();

    std::vector<BValue> operands;
    operands.reserve(2);
    operands.emplace_back(builder_.Ne(
        value1, builder_.Literal(UBits(0U, sz1), SourceInfo(), "const_0")));
    operands.emplace_back(builder_.Ne(
        value2, builder_.Literal(UBits(0U, sz2), SourceInfo(), "const_0")));

    BValue result;
    if (node->GetOpKind() == OpKind::kLogicalAnd) {
      result = builder_.And(operands);
    } else {
      result = builder_.Or(operands);
    }
    return result;
  }

  std::vector<BValue> operands;
  auto expr_l = VisitExpr(node->lhs(), ctx);
  auto expr_r = VisitExpr(node->rhs(), ctx);
  auto size_l = expr_l.BitCountOrDie();
  auto size_r = expr_r.BitCountOrDie();
  if (size_l > size_r) {
    operands.emplace_back(expr_l);
    operands.emplace_back(ChangeSize(expr_r, size_l));
  } else {
    operands.emplace_back(ChangeSize(expr_l, size_r));
    operands.emplace_back(expr_r);
  }
  BValue value;
  switch (node->GetOpKind()) {
  case OpKind::kMinus:
    value = builder_.Subtract(operands.at(0), operands.at(1), SourceInfo(),
                              name.value_or(""));
    break;
  case OpKind::kPlus:
    value = builder_.Add(operands.at(0), operands.at(1), SourceInfo(),
                         name.value_or(""));
    break;
  case OpKind::kMul:
    value = builder_.UMul(operands.at(0), operands.at(1), SourceInfo(),
                          name.value_or(""));
    break;
  case OpKind::kDiv:
    value = builder_.UDiv(operands.at(0), operands.at(1), SourceInfo(),
                          name.value_or(""));
    break;
  case OpKind::kBitwiseAnd:
    value = builder_.And(operands, SourceInfo(), name.value_or(""));
    break;
  case OpKind::kBitwiseOr:
    value = builder_.Or(operands, SourceInfo(), name.value_or(""));
    break;
  case OpKind::kEqual:
    value = builder_.Eq(operands.at(0), operands.at(1), SourceInfo(),
                        name.value_or(""));
    break;
  case OpKind::kNotEqual:
    value = builder_.Ne(operands.at(0), operands.at(1), SourceInfo(),
                        name.value_or(""));
    break;
  case OpKind::kGreaterEqual:
    value = builder_.UGe(operands.at(0), operands.at(1), SourceInfo(),
                         name.value_or(""));
    break;
  case OpKind::kGreaterThan:
    value = builder_.UGt(operands.at(0), operands.at(1), SourceInfo(),
                         name.value_or(""));
    break;
  case OpKind::kLessEqual:
    value = builder_.ULe(operands.at(0), operands.at(1), SourceInfo(),
                         name.value_or(""));
    break;
  case OpKind::kLessThan:
    value = builder_.ULt(operands.at(0), operands.at(1), SourceInfo(),
                         name.value_or(""));
    break;
  default:
    XLS_LOG(FATAL) << "not a binary operation, content : "
                   << node->ToString(2, 0) << std::endl;
    break;
  }
  return value;
}

BValue
P5ActionIrConverter::VisitUnaryOpExpr(UnaryOpExpr *node, const ContextType &ctx,
                                      absl::optional<absl::string_view> name) {
  auto operand = node->operand();
  BValue operand_value = VisitExpr(operand, ctx);
  BValue result;
  switch (node->GetOpKind()) {
  case OpKind::kBitwiseNot:
  case OpKind::kLogicalNot: {
    result = builder_.Not(operand_value, SourceInfo(), name.value_or(""));
    break;
  }
  default:
    XLS_LOG(FATAL) << "not a unary operation, content : "
                   << node->ToString(2, 0) << std::endl;
  }
  return result;
}

BValue P5ActionIrConverter::VisitBuiltinCallExpr(
    BuiltinCallExpr *node, const ContextType &ctx,
    absl::optional<absl::string_view> name) {
  auto callee = node->callee();
  auto args = node->args();
  XLS_CHECK((callee == "sizeof" || callee == "_get_anchor" ||
             callee == "_stack_push_h" || callee == "_stack_push_b") &&
            args.size() == 1U);
  BValue result;
  if (callee == "sizeof") {
    auto arg_value = VisitExpr(args.at(0), ctx);
    auto size = arg_value.GetType()->GetFlatBitCount();
    result = builder_.Literal(UBits(size, 64), SourceInfo(),
                              absl::StrFormat("const_%d", size));
  } else {
    // TODO: handle other built-in function call
    result = VisitExpr(args.at(0), ctx);
  }
  return result;
}

} // namespace
absl::StatusOr<std::unique_ptr<Package>>
ConvertP5AstModuleToPackage(Module *ast_module, LoweringMappingRel *mapping) {
  LoweringInfo lowering_info;
  XLS_RETURN_IF_ERROR(LoweringTransform(&lowering_info, ast_module,
                                        /* delimiter */ ".",
                                        /*should verify*/ true, mapping));

  AstAnalysisInformation analysis(ast_module->body);

  constexpr auto func_name = "action";
  constexpr auto package_name = "p5";

  std::unique_ptr<Package> package = std::make_unique<Package>(package_name);

  P5ActionIrConverter converter(
      ast_module->body, func_name /* generated function name */, package.get(),
      analysis, mapping, true /* should verify IR*/);
  auto function = converter.Build();
  if (!function.ok()) {
    XLS_LOG(WARNING) << "Fail to convert an action: " << function.status();
    return function.status();
  }

  return std::move(package);
}

} // namespace xls::p5
