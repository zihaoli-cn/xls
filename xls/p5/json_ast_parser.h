#pragma once

#include "absl/status/statusor.h"
#include "xls/p5/ast.h"
#include "xls/p5/lowering_mapping.h"
#include "xls/p5/util/json.hpp"
#include <memory>

namespace xls::p5 {

// Parse Module from json-format P5 AST
// Args:
//   json: the loaded json
//     remark: Actually, the function just load AST from json and will not
//     change it. But as you can see here, we use ref. rather than const ref.
//     The reason to do this is that we might further add some information to
//     the json by referecencing the Lowering Mapping Relationship
//   mapping: maintain the mapping relationship during lowering
//     remark: if you don't need mapping relationship, send `nullptr`
// Returns:
//   `Module` kind AstNode
absl::StatusOr<std::unique_ptr<Module>>
ParseModuleFromJson(nlohmann::json &json,
                    LoweringMappingRel *mapping = nullptr);

}; // namespace xls::p5