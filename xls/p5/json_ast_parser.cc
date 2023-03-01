#include "xls/p5/json_ast_parser.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "xls/common/logging/logging.h"
#include "xls/p5/keywords.h"
#include "absl/strings/string_view.h"
#include <fstream>
#include <string>
namespace xls::p5 {

// anonymous namespace for Json AST Parser
namespace {

class JsonSyntaxTreeParser {
public:
  explicit JsonSyntaxTreeParser(LoweringMappingRel *mapping)
      : mapping_(mapping) {
    // register all Binary OP
#define INSERT_BINARY_OP(_str, _enum) binary_op_[_str] = OpKind::_enum;
    P5_BINARY_OP(INSERT_BINARY_OP)
#undef INSERT_BINARY_OP

    // register all Unary OP
#define INSERT_UNARY_OP(_str, _enum) unary_op_[_str] = OpKind::_enum;
    P5_UNARY_OP(INSERT_UNARY_OP)
#undef INSERT_UNARY_OP
  }

  // Parse AST Module from the given json
  [[nodiscard]] absl::StatusOr<std::unique_ptr<Module>>
  parse(nlohmann::json &json) const;

private:
  absl::StatusOr<StmtBlock *> ParseStmtBlock(nlohmann::json &json,
                                             Module *module) const;

  absl::StatusOr<AssignStmt *> ParseAssignStmt(nlohmann::json &json,
                                               Module *module) const;

  absl::StatusOr<IfElseStmt *> ParseIfElseStmt(nlohmann::json &json,
                                               Module *module) const;

  absl::StatusOr<IfStmt *> ParseIfStmt(nlohmann::json &json,
                                       Module *module) const;

  absl::StatusOr<ReturnStmt *> ParseReturnStmt(nlohmann::json &json,
                                               Module *module) const;

  absl::StatusOr<NopStmt *> ParseNopStmt(nlohmann::json &json,
                                         Module *module) const;

  absl::StatusOr<ExprEvalStmt *> ParseExprEvalStmt(nlohmann::json &json,
                                                   Module *module) const;

  absl::StatusOr<Stmt *> ParseStmt(nlohmann::json &json, Module *module) const;

  absl::StatusOr<Expr *> ParseExpr(nlohmann::json &json, Module *module) const;

  absl::StatusOr<Lvalue *> ParseLvalue(nlohmann::json &json,
                                       Module *module) const;

  absl::StatusOr<NameRefExpr *> ParseNameRefExpr(nlohmann::json &json,
                                                 Module *module) const;

  absl::StatusOr<IntLiteralExpr *> ParseNamedConstantExpr(nlohmann::json &json,
                                                 Module *module) const;

  absl::StatusOr<FieldAccessExpr *> ParseFieldAccessExpr(nlohmann::json &json,
                                                         Module *module) const;

  absl::StatusOr<BitSliceExpr *> ParseSliceExpr(nlohmann::json &json,
                                                Module *module) const;

  absl::StatusOr<CastExpr *> ParseCastExpr(nlohmann::json &json,
                                           Module *module) const;

  absl::StatusOr<TypeAnnotation *> ParseTypeAnnotation(nlohmann::json &json,
                                                       Module *module) const;

  absl::StatusOr<BuiltinCallExpr *> ParseBuiltinCall(nlohmann::json &json,
                                                     Module *module) const;

  absl::StatusOr<BinaryOpExpr *> ParseBinaryOpExpr(nlohmann::json &json,
                                                   Module *module) const;

  absl::StatusOr<UnaryOpExpr *> ParseUnaryOpExpr(nlohmann::json &json,
                                                 Module *module) const;

  absl::StatusOr<IntLiteralExpr *> ParseIntLiteralExpr(nlohmann::json &json,
                                                       Module *module) const;

  absl::StatusOr<LongIntLiteralExpr *>
  ParseLongIntLiteralExpr(nlohmann::json &json, Module *module) const;

  absl::StatusOr<ArrIndexExpr *> ParseArrIndexExpr(nlohmann::json &json,
                                                   Module *module) const;

  absl::StatusOr<IntLiteralExpr *> ParseTrueFalseLiteral(nlohmann::json &json,
                                                         Module *module) const;

private:
  LoweringMappingRel *mapping_;
  std::unordered_map<std::string, OpKind> binary_op_;
  std::unordered_map<std::string, OpKind> unary_op_;
};

// Check whether it is an empty block
inline bool IsEmptyBlock(nlohmann::json &json) {
  return json.is_object() && json.at("TYNAME") == "BLOCK" &&
         ((!json.contains("OP1")) || (json.at("OP1").at("TYNAME") == "LIST" && json.at("OP1").at("VALUES").size() == 0)) ;
}


// AST Type: Statement Block
// C-like Grammar example:
//   {
//     stmt;
//     ...
//     stmt;
//   }
// Json field format:
//   json["TYNAME"] == "BLOCK"
//   json["OP0"] : Identifier Type, serves as block's name
//   json["OP1"] : List of Stmt or Inner Nested Block,
//     or no such field, which means this is a empty block
absl::StatusOr<StmtBlock *>
JsonSyntaxTreeParser::ParseStmtBlock(nlohmann::json &json,
                                     Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") && json.contains("OP0") &&
        json.at("TYNAME") == "BLOCK")) {
    XLS_LOG(WARNING) << "Invalid json to parse a `StmtBlock` Stmt, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `StmtBlock` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }

  if (json.contains("OP1") && json.at("OP1").at("TYNAME") == "BLOCK") {
    // Inner Nested Block
    return ParseStmtBlock(json.at("OP1"), module);
  } else {
    std::string name = json.at("OP0").at("STRING");

    std::vector<Stmt *> stmts;
    if (json.contains("OP1")) {
      nlohmann::json &block = json.at("OP1");
      XLS_CHECK(block.at("TYNAME") == "LIST");
      for (auto &elem : block.at("VALUES")) {
        if(IsEmptyBlock(elem)){
          continue;
        }
        auto stmt = ParseStmt(elem, module);
        if (!stmt.ok()) {
          XLS_LOG(WARNING) << "Fail to parse a stmt in `StmtBlock`, error msg:"
                           << stmt.status();
          return stmt.status();
        }
        stmts.push_back(*stmt);
      }
    }
    return module->AddStmtBlock(name, stmts);
  }
}

// Check whether it is an single element block
inline bool IsSingleElementBlock(nlohmann::json &json) {
  if(json.is_object() && json.at("TYNAME") == "BLOCK" && json.contains("OP1")){
    auto op1 = json.at("OP1");
    if(op1.at("TYNAME") == "LIST"){
      return op1.at("VALUES").size() == 1;
    }
    return true;
  }
  return false;
}

inline nlohmann::json & GetSingleElement(nlohmann::json &json){
  XLS_CHECK(IsSingleElementBlock(json));
  return json.at("OP1").at("VALUES").back();
}

// AST Type: Assignment Statement
// C-like Grammar example:
//   lhs = rhs-expr;
// Json field format:
//   json["TYNAME"] == "ASSIGN"
//   json["OP0"] : Lvalue Type, the rhs of the assignment
//     e.g., `BitSlice`, `ArrIndex`, `NameRef`, `FieldAccess`
//   json["OP1"] : Expr Type, the rhs of the assignment
absl::StatusOr<AssignStmt *>
JsonSyntaxTreeParser::ParseAssignStmt(nlohmann::json &json,
                                      Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        json.at("TYNAME") == "ASSIGN" && json.contains("OP0") &&
        json.contains("OP1"))) {
    XLS_LOG(WARNING) << "Invalid json to parse a `AssignStmt` Stmt, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `AssignStmt` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }

  auto lhs = ParseLvalue(json.at("OP0"), module);
  if (!lhs.ok()) {
    XLS_LOG(WARNING) << "Fail to parse a the lhs in `AssignStmt`, error msg:"
                     << lhs.status();
    return lhs.status();
  }

  auto rhs = ParseExpr(json.at("OP1"), module);
  if (!rhs.ok()) {
    XLS_LOG(WARNING) << "Fail to parse a the rhs in `AssignStmt`, error msg:"
                     << rhs.status();
    return rhs.status();
  }

  return module->AddAssignStmt(*lhs, *rhs);
}

// AST Type: If-else Statement
// C-like Grammar example:
//   if(expr) stmt else stmt
// Json field format:
//   json["TYNAME"] == "IF"
//   json["OP0"] : Expr Type, the branch condition
//   json["OP1"] : Stmt Type, the then-clause
//   json["OP2"] : Stmt Type, the else-clause
//     note that the else-clause SHOULD NOT be empty block,
//     if so, it is handled by `If Statement`
absl::StatusOr<IfElseStmt *>
JsonSyntaxTreeParser::ParseIfElseStmt(nlohmann::json &json,
                                      Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        json.at("TYNAME") == "IF" && json.contains("OP0") &&
        json.contains("OP1") && json.contains("OP2") &&
        !IsEmptyBlock(json.at("OP2")))) {
    XLS_LOG(WARNING) << "Invalid json to parse a `IfElseStmt` Stmt, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `IfElseStmt` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }

  auto expr = ParseExpr(json.at("OP0"), module);
  if (!expr.ok()) {
    XLS_LOG(WARNING)
        << "Fail to parse the condition in `IfElseStmt`, error msg:"
        << expr.status();
    return expr.status();
  }

  auto then_block = ParseStmt(json.at("OP1"), module);
  if (!then_block.ok()) {
    XLS_LOG(WARNING)
        << "Fail to parse the then-clause in `IfElseStmt`, error msg:"
        << then_block.status();
    return then_block.status();
  }

  auto else_block = ParseStmt(json.at("OP2"), module);
  if (!else_block.ok()) {
    XLS_LOG(WARNING)
        << "Fail to parse the else-clause in `IfElseStmt`, error msg:"
        << else_block.status();
    return else_block.status();
  }

  return module->AddIfElseStmt(*expr, *then_block, *else_block);
}

// AST Type: If Statement
// C-like Grammar example:
//   if(expr) stmt
// Json field format:
//   json["TYNAME"] == "IF"
//   json["OP0"] : Expr Type, the branch condition
//   json["OP1"] : Stmt Type, the then-clause
//   json["OP2"] : Stmt Type, empty block, or there is no such field
absl::StatusOr<IfStmt *>
JsonSyntaxTreeParser::ParseIfStmt(nlohmann::json &json, Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        json.at("TYNAME") == "IF" && json.contains("OP0") &&
        json.contains("OP1") &&
        (!json.contains("OP2") || IsEmptyBlock(json.at("OP2"))))) {
    XLS_LOG(WARNING) << "Invalid json to parse a `IfStmt` Stmt, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `IfStmt` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }

  auto expr = ParseExpr(json.at("OP0"), module);
  if (!expr.ok()) {
    XLS_LOG(WARNING) << "Fail to parse the condition in `IfStmt`, error msg:"
                     << expr.status();
    return expr.status();
  }

  auto then_block = ParseStmt(json.at("OP1"), module);
  if (!then_block.ok()) {
    XLS_LOG(WARNING) << "Fail to parse the then-clause in `IfStmt`, error msg:"
                     << then_block.status();
    return then_block.status();
  }

  return module->AddIfStmt(*expr, *then_block);
}

// AST Type: Return Statement
// C-like Grammar example:
//   return;
//   // there is no return value
// Json field format:
//   json["TYNAME"] == "RETURN"
absl::StatusOr<ReturnStmt *>
JsonSyntaxTreeParser::ParseReturnStmt(nlohmann::json &json,
                                      Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        json.at("TYNAME") == "RETURN")) {
    XLS_LOG(WARNING) << "Invalid json to parse a `ReturnStmt` Stmt, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `ReturnStmt` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }
  return module->AddReturnStmt();
}

// AST Type: Nop Statement
// C-like Grammar example:
//   nop;
//   // there is no return value
// Json field format:
//   json["TYNAME"] == "NOP"
absl::StatusOr<NopStmt *>
JsonSyntaxTreeParser::ParseNopStmt(nlohmann::json &json, Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        json.at("TYNAME") == "NOP")) {
    XLS_LOG(WARNING) << "Invalid json to parse a `NopStmt` Stmt, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `NopStmt` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }
  return module->AddNopStmt();
}

// AST Type: Expr Eval Statement
// C-like Grammar example:
//   expr;
//   // no assignment, only eval the expr
//   // usually, the reason to do this is to complete the side effects
// Json field format:
//   json["TYNAME"] == "FUNCTION_CALL"
//     currently, the expr to eval is the Builtin-Call
absl::StatusOr<ExprEvalStmt *>
JsonSyntaxTreeParser::ParseExprEvalStmt(nlohmann::json &json,
                                        Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        json.at("TYNAME") == "FUNCTION_CALL")) {
    XLS_LOG(WARNING) << "Invalid json to parse a `ExprEvalStmt` Stmt, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `ExprEvalStmt` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }

  auto call = ParseBuiltinCall(json, module);
  if (!call.ok()) {
    XLS_LOG(WARNING)
        << "Fail to parse the built-in call in `ExprEvalStmt`, error msg:"
        << call.status();
    return call.status();
  }

  return module->AddExprEvalStmt(*call);
}

// AST Type: All Statement Types
// Json field format:
//   json["TYNAME"] :
//     parse different Leaf Statement Type by checking "TYNAME"
absl::StatusOr<Stmt *> JsonSyntaxTreeParser::ParseStmt(nlohmann::json &json,
                                                       Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME"))) {
    XLS_LOG(WARNING) << "Invalid json to parse a Stmt, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `Stmt` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }

  std::string type_name = json.at("TYNAME");
  absl::StatusOr<Stmt *> result;
  if (type_name == "IF") {
    if (json.contains("OP2") && !IsEmptyBlock(json.at("OP2"))) {
      result = ParseIfElseStmt(json, module);
    } else {
      result = ParseIfStmt(json, module);
    }
  } else if (type_name == "ASSIGN") {
    result = ParseAssignStmt(json, module);
  } else if (type_name == "RETURN") {
    result = ParseReturnStmt(json, module);
  } else if (type_name == "NOP") {
    result = ParseNopStmt(json, module);
  } else if (type_name == "BLOCK") {
    if(IsSingleElementBlock(json)){
      result = ParseStmt(GetSingleElement(json), module);
    }
     else{
    result = ParseStmtBlock(json, module);
    }
  } else if (type_name == "FUNCTION_CALL") {
    result = ParseExprEvalStmt(json, module);
  } else {
    XLS_LOG(WARNING) << "Unsuppored Stmt kind, TYNAME: " << type_name
                     << ", content : " << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `Stmt` from json, but this is not a "
                     "supported stmt type, content : \n",
                     json.dump(2)));
  }

  if (!result.ok()) {
    XLS_LOG(WARNING) << "Fail to parse a specific kind of Stmt, error msg:"
                     << result.status() << ", content : " << json.dump(2);
    return result.status();
  }

  if (mapping_) {
    // save lowering mapping
    mapping_->ast2json.insert(
        std::pair<AstNode *, nlohmann::json &>(*result, json));
  }

  return result;
}

// AST Type: Enum or other Named Constant, such as "true", "false"
// Json field format:
//   json["TYNAME"] : "IDENT"
//   json["VALUE"] : constant value
//   json["STRING"] : name
absl::StatusOr<IntLiteralExpr *> JsonSyntaxTreeParser::ParseNamedConstantExpr(nlohmann::json &json,
                                                 Module *module) const
{
  if (!(json.is_object() && json.contains("TYNAME") &&
        json.at("TYNAME") == "IDENT" && json.contains("STRING") && json.contains("VALUE"))) {
    XLS_LOG(WARNING) << "Invalid json to parse a `NamedContant` expr, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `NamedContantExpr` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }

  IntLiteralExpr::size_type size = 64U;
  IntLiteralExpr::value_type value = json.at("VALUE");
  std::string name = json.at("STRING");
  if(name == "true" || name == "false"){
    size = 1;
  }
  
  return module->AddIntLiteralExpr(value, size, name);
}

// AST Type: All Expr Types
// Json field format:
//   json["TYNAME"] :
//     parse different Leaf Expr Type by checking "TYNAME"
absl::StatusOr<Expr *> JsonSyntaxTreeParser::ParseExpr(nlohmann::json &json,
                                                       Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME"))) {
    XLS_LOG(WARNING) << "Invalid json to parse a Expr, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `Expr` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }

  std::string type_name = json.at("TYNAME");
  absl::StatusOr<Expr *> result;
  if (type_name == "IDENT") {
    if(json.contains("VALUE")){
      result = ParseNamedConstantExpr(json, module);
    } else {
      result = ParseNameRefExpr(json, module);
    }
  } else if (type_name == "DOT") {
    result = ParseFieldAccessExpr(json, module);
  } else if (type_name == "SLICE") {
    if (!(json.at("OP1").contains("INT") || json.at("OP2").contains("INT"))) {
      // full bit slice, which is equivalent to the referenced expr
      //   expr[:] <=> expr
      result = ParseExpr(json.at("OP0"), module);
    } else {
      result = ParseSliceExpr(json, module);
    }
  } else if (type_name == "INT_LIT") {
    result = ParseIntLiteralExpr(json, module);
  } else if (type_name == "FUNCTION_CALL") {
    result = ParseBuiltinCall(json, module);
  } else if (type_name == "CAST") {
    result = ParseCastExpr(json, module);
  } else if (type_name == "INDEX") {
    result = ParseArrIndexExpr(json, module);
  } else if (type_name == "LIST") {
    result = ParseLongIntLiteralExpr(json, module);
  } else if (binary_op_.find(type_name) != binary_op_.end()) {
    // binary_op_.contains(type_name)
    result = ParseBinaryOpExpr(json, module);
  } else if (unary_op_.find(type_name) != unary_op_.end()) {
    // unary_op_.contains(type_name)
    result = ParseUnaryOpExpr(json, module);
  } else {
    XLS_LOG(WARNING) << "Unsuppored `Expr` kind, TYNAME: " << type_name
                     << ", content : " << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `Expr` from json, but this is not a "
                     "supported expr type, content : \n",
                     json.dump(2)));
  }

  if (!result.ok()) {
    XLS_LOG(WARNING) << "Fail to parse a specific kind of Expr, error msg:"
                     << result.status() << ", content : " << json.dump(2);
    return result.status();
  }

  if (mapping_) {
    mapping_->ast2json.insert(
        std::pair<AstNode *, nlohmann::json &>(*result, json));
  }
  return result;
}

// AST Type: All Lvalue Types
// Json field format:
//   json["TYNAME"] :
//     parse different Leaf Expr Type by checking "TYNAME"
absl::StatusOr<Lvalue *>
JsonSyntaxTreeParser::ParseLvalue(nlohmann::json &json, Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME"))) {
    XLS_LOG(WARNING) << "Invalid json to parse a Lvalue, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `Lvalue` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }

  std::string type_name = json.at("TYNAME");
  absl::StatusOr<Lvalue *> result;
  if (type_name == "IDENT") {
    result = ParseNameRefExpr(json, module);
  } else if (type_name == "DOT") {
    result = ParseFieldAccessExpr(json, module);
  } else if (type_name == "SLICE") {
    if (!(json.at("OP1").contains("INT") || json.at("OP2").contains("INT"))) {
      // full bit slice, which is equivalent to the referenced expr
      //   expr[:] <=> expr
      result = ParseLvalue(json.at("OP1"), module);
    } else {
      result = ParseSliceExpr(json, module);
    }
  } else if (type_name == "INDEX") {
    result = ParseArrIndexExpr(json, module);
  } else {
    XLS_LOG(WARNING) << "Unsuppored `Lvalue` kind, TYNAME: " << type_name
                     << ", content : " << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `Lvalue` from json, but this is not a "
                     "supported expr type, content : \n",
                     json.dump(2)));
  }

  if (!result.ok()) {
    XLS_LOG(WARNING) << "Fail to parse a specific kind of Lvalue, error msg:"
                     << result.status() << ", content : " << json.dump(2);
    return result.status();
  }

  if (mapping_) {
    mapping_->ast2json.insert(
        std::pair<AstNode *, nlohmann::json &>(*result, json));
  }

  return result;
}

// AST Type: Name Reference Expr
// C-like Grammar example:
//   identifier
// Json field format:
//   json["TYNAME"] == "IDENT"
//   json["STRING"] : the identifier's name
//   json["GLOBAL"] : whether the variable is global
//   json["SIZE"] : the total bit size of the variable
// Note:
//   "SIZE" and "GLOBAL" fields : has both or neither
absl::StatusOr<NameRefExpr *>
JsonSyntaxTreeParser::ParseNameRefExpr(nlohmann::json &json,
                                       Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        json.at("TYNAME") == "IDENT" && json.contains("STRING") && !json.contains("VALUE")
       )
     ) {
    XLS_LOG(WARNING) << "Invalid json to parse a `NameRefExpr` expr, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `NameRefExpr` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }

  bool has_size = json.contains("SIZE");
  bool has_global = json.contains("GLOBAL");
  bool has_both_fields = (has_size && has_global);
  bool has_neither_fields = !(has_size || has_global);

  // "SIZE" and "GLOBAL" fields : has both or neither
  XLS_CHECK(has_both_fields || has_neither_fields);

  auto result = module->AddNameRefExpr(json.at("STRING"));

  if(has_both_fields){
    uint32_t size = json.at("SIZE");
    bool is_global = (json.at("GLOBAL") == 1);

    NameRefExpr::Annotation anno(size, is_global);
    result->AddAnnotation(anno);
  }

  return result;
}

// AST Type: Field Access Expr
// C-like Grammar example:
//   expr.identifier
// Json field format:
//   json["TYNAME"] == "DOT"
//   json["OP0"] : Lvalue Type, the target to access field
//   json["OP1"] : Identifier Type, the field's name
//   json["SIZE"] : field bitcount
//   json["GLOBAL"] : whether the variable is global
//   json["STRUCT"] : e.g., for expr `a.b.c.d`, then this field will be "a"
//   json["OFFSET"] : the starting offset for the field in the variable
//     currently, we don't know whether this is a Union or Struct
absl::StatusOr<FieldAccessExpr *>
JsonSyntaxTreeParser::ParseFieldAccessExpr(nlohmann::json &json,
                                           Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        "DOT" == json.at("TYNAME") && json.contains("OP0") &&
        json.contains("OP1"))) {
    XLS_LOG(WARNING)
        << "Invalid json to parse a `FieldAccessExpr` Expr, content: "
        << json.dump(2);

    return absl::InvalidArgumentError(absl::StrCat(
        "Try parse a `FieldAccessExpr` from json, but fail to check "
        "type, content : \n",
        json.dump(2)));
  }

  std::string field_name = json.at("OP1").at("STRING");

  auto source = ParseLvalue(json.at("OP0"), module);
  if (!source.ok()) {
    XLS_LOG(WARNING)
        << "Fail to parse the target lvalue in `FieldAccessExpr`, error msg:"
        << source.status();
    return source.status();
  }

  auto result = module->AddFieldAccessExpr(*source, field_name);

  bool has_size = json.contains("SIZE");
  bool has_global = json.contains("GLOBAL");
  bool has_struct = json.contains("STRUCT");
  bool has_offset = json.contains("OFFSET");
  
  bool has_all = (has_size && has_global && has_struct && has_offset);
  bool has_none = !(has_size || has_global || has_struct || has_offset);

  XLS_CHECK(has_all || has_none);

  if(has_all){
    uint32_t size = json.at("SIZE");
    bool is_global = json.at("GLOBAL") == 1;
    std::string struct_var_name = json.at("STRUCT");
    uint32_t offset = json.at("OFFSET");

    FieldAccessExpr::Annotation anno(size, is_global, struct_var_name, offset);
    result->AddAnnotation(anno);
  }

  return result;
}

// The Literal in json is a reversed ordered
inline int64_t GetReversedInt64Value(std::string str) {
  return std::stoll(std::string(str.rbegin(), str.rend()));
}

// AST Type: Bit Slice Expr
// C-like Grammar example:
//   expr[literal:literal]
// Json field format:
//   json["TYNAME"] == "SLICE"
//   json["OP0"] : Lvalue Type, the target to access field
//   json["OP1"] : IntLiteral Type, msb
//   json["OP2"] : IntLiteral Type, lsb
absl::StatusOr<BitSliceExpr *>
JsonSyntaxTreeParser::ParseSliceExpr(nlohmann::json &json,
                                     Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        "SLICE" == json.at("TYNAME") && json.contains("OP0") &&
        json.contains("OP1") && json.contains("OP2") &&
        (json.at("OP1").contains("INT") || json.at("OP2").contains("INT")))) {

    XLS_LOG(WARNING) << "Invalid json to parse a `BitSliceExpr` expr, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `BitSliceExpr` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }

  auto expr = ParseExpr(json.at("OP0"), module);
  if (!expr.ok()) {
    XLS_LOG(WARNING)
        << "Fail to parse the expr to slice in `BitSliceExpr`, error msg:"
        << expr.status();
    return expr.status();
  }

  uint32_t max_bit = 0;
  if (json.at("OP1").contains("INT")) {
    max_bit = GetReversedInt64Value(json.at("OP1").at("INT"));
  }
  uint32_t min_bit = 0U;
  if (json.at("OP2").contains("INT")) {
    min_bit = GetReversedInt64Value(json.at("OP2").at("INT"));
  }

  XLS_CHECK(max_bit >= min_bit && max_bit >= 0);

  return module->AddBitSliceExpr(*expr, max_bit, min_bit);
}

// AST Type: Casting Expr
// C-like Grammar example:
//   (type)expr
// Json field format:
//   json["TYNAME"] == "CAST"
//   json["OP0"] : Expr Type, the expr to cast
//   json["OP1"] : TypeAnnotaion/NameRef Type
//     TypeAnnotation directly gives the bitcount
//     NameRef provides the targeting TypeName to cast
absl::StatusOr<CastExpr *>
JsonSyntaxTreeParser::ParseCastExpr(nlohmann::json &json,
                                    Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        "CAST" == json.at("TYNAME") && json.contains("OP0") &&
        json.contains("OP1"))) {
    XLS_LOG(WARNING) << "Invalid json to parse a `CastExpr` expr, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `CastExpr` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }

  auto expr_to_cast = ParseExpr(json.at("OP0"), module);
  if (!expr_to_cast.ok()) {
    XLS_LOG(WARNING)
        << "Fail to parse the expr-to-cast in `CastExpr`, error msg:"
        << expr_to_cast.status();
    return expr_to_cast.status();
  }

  absl::StatusOr<TypeAnnotation *> cast_to = ParseTypeAnnotation(json.at("OP1"), module);
  
  if (!cast_to.ok()) {
    XLS_LOG(WARNING)
        << "Fail to parse the TypeAnnotation/TypeName in `CastExpr`, error msg:"
        << cast_to.status();
    return cast_to.status();
  }

  return module->AddCastExpr(*expr_to_cast, *cast_to);
}

// AST Type: TypeAnnotation
// C-like Grammar example:
//   uint<IntLiteral>
// Json field format:
//   json["TYNAME"] == "IDENT"
//   json["STRING"] : type name
//   json["SIZE"] : bitcount // used in early version, deprecated
//   json["TYPESIZE"] : bitcount, available if it is not uint type
absl::StatusOr<TypeAnnotation *>
JsonSyntaxTreeParser::ParseTypeAnnotation(nlohmann::json &json,
                                          Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        json.at("TYNAME") == "IDENT" && json.contains("TYPESIZE")
       )
     ) {
    XLS_LOG(WARNING) << "Invalid json to parse a `TypeAnnotation`, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(absl::StrCat(
        "Try parse a `TypeAnnotation` from json, but fail to check "
        "type, content : \n",
        json.dump(2)));
  }

  uint32_t size = json.at("TYPESIZE");
  std::string name = json.at("STRING");

  return module->AddTypeAnnotation(size, name);
}

// AST Type: Built-in Call Expr
// C-like Grammar example:
//   identifier(expr, expr, ...)
// Json field format:
//   json["TYNAME"] == "FUNCTION_CALL"
//   json["OP0"] : Identifier Type, the builtin function name
//   json["OP1"] : List of Expr Type
absl::StatusOr<BuiltinCallExpr *>
JsonSyntaxTreeParser::ParseBuiltinCall(nlohmann::json &json,
                                       Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        json.at("TYNAME") == "FUNCTION_CALL" && json.contains("OP0") &&
        json.contains("OP1"))) {
    XLS_LOG(WARNING)
        << "Invalid json to parse a `BuiltinCallExpr` expr, content: "
        << json.dump(2);
    return absl::InvalidArgumentError(absl::StrCat(
        "Try parse a `BuiltinCallExpr` from json, but fail to check "
        "type, content : \n",
        json.dump(2)));
  }

  std::string name = json.at("OP0").at("STRING");
  std::vector<Expr *> args;
  for (auto &arg : json.at("OP1").at("VALUES")) {
    auto expr = ParseExpr(arg, module);
    if (!expr.ok()) {
      XLS_LOG(WARNING)
          << "Fail to parse an arg in `BuiltinCallExpr`, error msg:"
          << expr.status();
      return expr.status();
    }
    args.push_back(*expr);
  }

  return module->AddBuiltinCallExpr(name, args);
}

// AST Type: BinaryOp Expr
// C-like Grammar example:
//   expr op expr
// Json field format:
//   json["TYNAME"] : binary op
//   json["OP0"] : Expr Type, operand1
//   json["OP1"] : Expr Type, operand2
absl::StatusOr<BinaryOpExpr *>
JsonSyntaxTreeParser::ParseBinaryOpExpr(nlohmann::json &json,
                                        Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        binary_op_.find(json.at("TYNAME")) != binary_op_.end() &&
        json.contains("OP0") && json.contains("OP1"))) {
    XLS_LOG(WARNING) << "Invalid json to parse a `BinaryOpExpr` expr, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `BinaryOpExpr` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }

  auto lhs = ParseExpr(json.at("OP0"), module);
  if (!lhs.ok()) {
    XLS_LOG(WARNING) << "Fail to parse lhs in `BinaryOpExpr`, error msg:"
                     << lhs.status();
    return lhs.status();
  }
  auto rhs = ParseExpr(json.at("OP1"), module);
  if (!rhs.ok()) {
    XLS_LOG(WARNING) << "Fail to parse lhs in `BinaryOpExpr`, error msg:"
                     << rhs.status();
    return rhs.status();
  }

  if (binary_op_.find(json.at("TYNAME")) == binary_op_.end()) {
    XLS_LOG(WARNING) << "No such binary OP, TYNAME : " << json.at("TYNAME");
    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `BinaryOpExpr` from json, but no such Binary "
                     "OP, content : \n",
                     json.dump(2)));
  }

  return module->AddBinaryOpExpr(binary_op_.at(json.at("TYNAME")), *lhs, *rhs);
}

// AST Type: UnaryOp Expr
// C-like Grammar example:
//   op expr
// Json field format:
//   json["TYNAME"] : unary op
//   json["OP0"] : Expr Type, operand
absl::StatusOr<UnaryOpExpr *>
JsonSyntaxTreeParser::ParseUnaryOpExpr(nlohmann::json &json,
                                       Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        unary_op_.find(json.at("TYNAME")) != unary_op_.end() &&
        json.contains("OP0"))) {
    XLS_LOG(WARNING) << "Invalid json to parse a `UnaryOpExpr` expr, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `UnaryOpExpr` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }

  // check whether op is valid
  if (unary_op_.find(json.at("TYNAME")) == unary_op_.end()) {
    XLS_LOG(WARNING) << "No such unary OP, TYNAME : " << json.at("TYNAME");

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `UnaryOpExpr` from json, but no such Unary "
                     "OP, content : \n",
                     json.dump(2)));
  }

  auto operand = ParseExpr(json.at("OP0"), module);
  if (!operand.ok()) {
    XLS_LOG(WARNING) << "Fail to parse lhs in `UnaryOpExpr`, error msg:"
                     << operand.status();
    return operand.status();
  }

  return module->AddUnaryOpExpr(unary_op_.at(json.at("TYNAME")), *operand);
}

// AST Type: Int Literal Expr
// Json field format:
//   json["TYNAME"] == INT_LIT
absl::StatusOr<IntLiteralExpr *>
JsonSyntaxTreeParser::ParseIntLiteralExpr(nlohmann::json &json,
                                          Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        json.at("TYNAME") == "INT_LIT")) {
    XLS_LOG(WARNING)
        << "Invalid json to parse a `IntLiteralExpr` expr, content: "
        << json.dump(2);

    return absl::InvalidArgumentError(absl::StrCat(
        "Try parse a `ParseIntLiteralExpr` from json, but fail to check "
        "type, content : \n",
        json.dump(2)));
  }

  int64_t value = 0LL;
  if (json.contains("INT")) {
    value = GetReversedInt64Value(json.at("INT"));
  }
  return module->AddIntLiteralExpr(value, 64U, "int_lit");
}

// AST Type: Long Int Literal Expr
// C-like Grammar example:
//   {literal, literal, ...}
// Json field format:
//   json["TYNAME"] == LIST
absl::StatusOr<LongIntLiteralExpr *>
JsonSyntaxTreeParser::ParseLongIntLiteralExpr(nlohmann::json &json,
                                              Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        json.at("TYNAME") == "LIST" && json.contains("VALUES"))) {
    XLS_LOG(WARNING)
        << "Invalid json to parse a `LongIntLiteralExpr` expr, content: "
        << json.dump(2);

    return absl::InvalidArgumentError(absl::StrCat(
        "Try parse a `LongIntLiteralExpr` from json, but fail to check "
        "type, content : \n",
        json.dump(2)));
  }

  uint32_t size = json.at("VALUES").size();
  std::vector<uint64_t> literals(size);
  uint32_t t = 0U;
  for (auto &u64 : json.at("VALUES")) {
    XLS_CHECK(u64.at("TYNAME") == "INT_LIT");
    int64_t value = 0LL;
    if (u64.contains("INT")) {
      value = GetReversedInt64Value(u64.at("INT"));
    }
    literals[t] = value;
    ++t;
  }
  XLS_CHECK(t == size);

  return module->AddLongIntLiteralExpr(std::move(literals));
}

// AST Type: Array Index Expr
// C-like Grammar example:
//   lvalue[literal]
// Json field format:
//   json["TYNAME"] == INDEX
//   json["OP0"] : Lvalue Type
//   json["OP1"] : IntLiteral Type
absl::StatusOr<ArrIndexExpr *>
JsonSyntaxTreeParser::ParseArrIndexExpr(nlohmann::json &json,
                                        Module *module) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        json.at("TYNAME") == "INDEX" && json.contains("OP1") &&
        json.at("OP1").at("TYNAME") == "INT_LIT" && json.contains("OP0"))) {
    XLS_LOG(WARNING) << "Invalid json to parse a `ArrIndexExpr` expr, content: "
                     << json.dump(2);

    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `ArrIndexExpr` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }

  uint32_t idx = 0U;
  if (json.at("OP1").contains("INT")) {
    idx = GetReversedInt64Value(json.at("OP1").at("INT"));
  }

  auto expr = ParseLvalue(json.at("OP0"), module);
  if (!expr.ok()) {
    XLS_LOG(WARNING) << "Fail to parse lvalue in `ArrIndexExpr`, error msg:"
                     << expr.status();
    return expr.status();
  }

  return module->AddArrIndexExpr(*expr, idx);
}

[[nodiscard]] absl::StatusOr<std::unique_ptr<Module>>
JsonSyntaxTreeParser::parse(nlohmann::json &json) const {
  if (!(json.is_object() && json.contains("TYNAME") &&
        json.at("TYNAME") == "BLOCK" && json.contains("OP0") &&
        json.contains("OP1"))) {
    return absl::InvalidArgumentError(
        absl::StrCat("Try parse a `Module` from json, but fail to check "
                     "type, content : \n",
                     json.dump(2)));
  }

  auto module = std::make_unique<Module>();
  auto body = ParseStmtBlock(json, module.get());
  if (!body.ok()) {
    XLS_LOG(ERROR) << body.status();
  }
  body.value()->SetParent(module.get());
  module->body = *body;
  XLS_CHECK(module->body != nullptr);
  return std::move(module);
}
} // namespace

// Wrap around the `JsonSyntaxTreeParser` class in the  anonymous namespace
absl::StatusOr<std::unique_ptr<Module>>
ParseModuleFromJson(nlohmann::json &json, LoweringMappingRel *mapping) {
  JsonSyntaxTreeParser parser(mapping);
  return parser.parse(json);
}

} // namespace xls::p5