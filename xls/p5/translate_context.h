#pragma once
#include "xls/ir/package.h"

#include "xls/p5/ast.h"
#include "xls/p5/json.hpp"
#include "xls/p5/lowering_mapping.h"

#include <memory>
#include <string>

namespace xls::p5 {

// Maintain the useful resources during Translation process
//   these resources' lifetime are expected to be the same
struct TranslateContext {
  std::string filename;                        // the json file's filename
  std::unique_ptr<nlohmann::json> loaded_json; // json
  std::unique_ptr<Module> p5_ast_module;       // AST
  std::unique_ptr<Package> xls_ir_package;     // IR

  LoweringMappingRel low2high_mapping;
};

} // namespace xls::p5