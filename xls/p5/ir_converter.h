#pragma once

#include "absl/status/statusor.h"
#include "xls/ir/package.h"
#include "xls/p5/ast.h"
#include "xls/p5/lowering_mapping.h"
#include <memory>
namespace xls::p5 {

// Converts the contents of a P5 AST module to XLS IR
// Args:
//   ast_module: the AST module to convert.
//   mapping: maintain the mapping relationship during lowering
//     note that if you don't need mapping relationship, use `nullptr`
// Returns:
//   The IR package that corresponds to this P5 AST module.
absl::StatusOr<std::unique_ptr<Package>>
ConvertP5AstModuleToPackage(Module *ast_module,
                            LoweringMappingRel *mapping = nullptr);

} // namespace xls::p5