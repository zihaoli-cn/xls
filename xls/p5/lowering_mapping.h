#pragma once

#include "xls/ir/node.h"
#include "xls/p5/ast.h"
#include "xls/p5/util/json.hpp"
#include <map>

namespace xls::p5 {

// Maintain the mapping relationship during lowering translation process
//   key-value: from low-level to high-level
struct LoweringMappingRel {
  bool is_valid; // if the IR is modified, then the mapping relationship
                 // is no longer meaningful

  std::map<xls::Node *, AstNode *> node2ast;      // ir node to AST node
  std::map<AstNode *, nlohmann::json &> ast2json; // AST node to json node
  std::map<AstNode *, AstNode *>
      ast2lowering; // lowering AST node to orgrinal AST node
};

} // namespace xls::p5