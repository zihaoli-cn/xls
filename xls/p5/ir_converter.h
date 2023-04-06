#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/variant.h"

#include "xls/ir/package.h"
#include "xls/p5/ast.h"
#include "xls/p5/lowering_mapping.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace xls::p5 {

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

// Collect those Array-Index Expr
struct ArrayIndexLoweringInfo {
  using ArrIndexMap = std::map<ArrIndexExpr *, VarRefExpr *>;

  std::set<ArrIndexExpr *> arr_indexes;
  std::set<FakeVarDef *>
      defs; // generated fake variables' def. for Array Index usage
  ArrIndexMap index_map;
};

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

// Collect those remaining NameRefExpr
struct NameRefLoweringInfo {
  using NameRefMap =
      std::map<NameRefExpr *,
               VarRefExpr *>; // map orignal AST node to lowered AST node

  std::set<NameRefExpr *> name_refs; // orginal NameRefExpr AST Node
  std::set<FakeVarDef *> defs;       // generated fake variables' def
  NameRefMap name_ref_map;
};

// Collect those Nested BitSlice Expr
struct NestedSliceLoweringInfo {
  using NestedSliceMap =
      std::map<BitSliceExpr *,
               BitSliceExpr *>; // map orignal AST node to lowered AST node

  std::set<BitSliceExpr *> nested_slices;
  NestedSliceMap nested_slice_map;
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
                               xls::p5::LoweringMappingRel *mapping = nullptr);

// All analysis information that are used for translation
//   - all global/local variables are gathered together in `variables`
//   - all return-stmts' hint conditions are predicted in `exits_predict_expr`
//   - the variables that each stmt may modify are saved in `stmt_modified_vars`
//   - all variable's size are known, i.e., FakeVarDef's size should not be
//      absl::nullopt
//      note that currently they are inferenced or generated to a fix size = 64
//      bit
using LiveRange = std::pair<uint32_t, uint32_t>;
using LiveRanges = std::vector<LiveRange>;
using LiveRangesMapPtrPair = std::pair<std::map<FakeVarDef *, LiveRanges> *,
                                       std::map<FakeVarDef *, LiveRanges> *>;
struct AstAnalysisInformation {
  absl::Status Analyze(StmtBlock *func_body);

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

struct IrConverterProfiler {
  int32_t active_ast_num1;
  absl::Duration tranform_duration;
  int32_t active_ast_num2;
  absl::Duration analysis_duration;
  int32_t active_ast_num3;
  absl::Duration conversion_duration;
};

// Converts the contents of a P5 AST module to XLS IR
// Args:
//   ast_module: the AST module to convert.
//   mapping: maintain the mapping relationship during lowering
//     note that if you don't need mapping relationship, use `nullptr`
// Returns:
//   The IR package that corresponds to this P5 AST module.
absl::StatusOr<std::unique_ptr<Package>>
ConvertP5AstModuleToPackage(Module *ast_module,
                            LoweringMappingRel *mapping = nullptr,
                            IrConverterProfiler *profiler = nullptr);

} // namespace xls::p5