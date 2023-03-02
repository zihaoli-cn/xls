
#include "xls/p5/ast.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "xls/common/logging/logging.h"

namespace xls::p5 {

void AstNode::SetParent(AstNode *parent) { parent_ = parent; }

BinaryOpExpr::BinaryOpExpr(OpKind op, Expr *lhs, Expr *rhs, Module *module)
    : Expr(AstNodeKind::kBinaryOpExpr, module), op_(op), lhs_(lhs), rhs_(rhs) {
  XLS_CHECK(IsBinaryOperator(op));
}

UnaryOpExpr::UnaryOpExpr(OpKind op, Expr *operand, Module *module)
    : Expr(AstNodeKind::kUnaryOpExpr, module), op_(op), operand_(operand) {
  XLS_CHECK(IsUnaryOperator(op));
}

std::string BinaryOpExpr::ToString(uint32_t indent, uint32_t pad) const {
  return "(" + OpKindToString(GetOpKind()) + " " +
         lhs()->ToString(indent, pad) + " " + rhs()->ToString(indent, pad) +
         ")";
}
bool BinaryOpExpr::ReplaceChild(AstNode *child, AstNode *dst) {
  XLS_CHECK(child->IsExprKind() && dst->IsExprKind());

  Expr *converted_child = (Expr *)child;
  Expr *converted_dst = (Expr *)dst;
  if (lhs_ == converted_child) {
    lhs_ = converted_dst;
    dst->SetParent(this);
    return true;
  }
  if (rhs_ == converted_child) {
    rhs_ = converted_dst;
    dst->SetParent(this);
    return true;
  }
  return false;
}

std::string UnaryOpExpr::ToString(uint32_t indent, uint32_t pad) const {
  return "(" + OpKindToString(GetOpKind()) + " " +
         operand()->ToString(indent, pad) + ")";
}
bool UnaryOpExpr::ReplaceChild(AstNode *child, AstNode *dst) {
  XLS_CHECK(child->IsExprKind() && dst->IsExprKind());
  Expr *converted_child = (Expr *)child;
  Expr *converted_dst = (Expr *)dst;
  if (operand_ == converted_child) {
    operand_ = converted_dst;
    dst->SetParent(this);
    return true;
  }
  return false;
}

std::string IntLiteralExpr::ToString(uint32_t indent, uint32_t pad) const {
  return absl::StrFormat("%llu/* <%ubits>%s */", value(), size(),
                         name().value_or(""));
}

std::string BuiltinCallExpr::ToString(uint32_t indent, uint32_t pad) const {
  std::string result(callee());
  result.push_back('(');
  for (auto arg : args_) {
    result += arg->ToString(indent, pad);
    result.push_back(',');
  }
  result.push_back(')');
  return result;
}

bool BuiltinCallExpr::ReplaceChild(AstNode *child, AstNode *dst) {
  XLS_CHECK(child->IsExprKind() && dst->IsExprKind());
  Expr *converted_child = (Expr *)child;
  Expr *converted_dst = (Expr *)dst;
  for (auto i = 0; i < args().size(); ++i) {
    if (args_[i] == converted_child) {
      args_[i] = converted_dst;
      dst->SetParent(this);
      return true;
    }
  }
  return false;
}

std::string CastExpr::ToString(uint32_t indent, uint32_t pad) const {
  return "(" + cast_to()->ToString(indent, pad) + ")" +
         expr_to_cast()->ToString(indent, pad);
}
bool CastExpr::ReplaceChild(AstNode *child, AstNode *dst) {
  if (child->GetKind() != AstNodeKind::kTypeAnnotation &&
      !child->IsExprKind()) {
    return false;
  }

  if (child->GetKind() == AstNodeKind::kTypeAnnotation &&
      ((TypeAnnotation *)child) == cast_to_) {
    cast_to_ = (TypeAnnotation *)dst;
    dst->SetParent(this);
    return true;
  } else if (child->IsExprKind() && ((Expr *)child) == expr_to_cast_) {
    expr_to_cast_ = (Expr *)dst;
    dst->SetParent(this);
    return true;
  }

  return false;
}

std::string NameRefExpr::ToString(uint32_t indent, uint32_t pad) const {
  return std::string(name());
}

std::string FieldAccessExpr::ToString(uint32_t indent, uint32_t pad) const {
  return source()->ToString(indent, pad) + "." + std::string(field_name());
}

std::string BitSliceExpr::ToString(uint32_t indent, uint32_t pad) const {
  return target()->ToString(indent, pad) + "[" + std::to_string(max_bit()) +
         ":" + std::to_string(min_bit()) + "]";
}
bool BitSliceExpr::ReplaceChild(AstNode *child, AstNode *dst) {
  XLS_CHECK(child->IsExprKind() && dst->IsExprKind());
  Expr *converted_child = (Expr *)child;
  Expr *converted_dst = (Expr *)dst;
  if (target_ == converted_child) {
    target_ = converted_dst;
    dst->SetParent(this);
    return true;
  }
  return false;
}

std::string StmtBlock::ToString(uint32_t indent, uint32_t pad) const {
  std::string left_padding = std::string(pad, ' ');
  std::string result =
      left_padding + "{ // block-name: " + std::string(name()) + "\n";

  std::string left_indent = left_padding + std::string(indent, ' ');
  for (auto stmt : stmts_) {
    result += stmt->ToString(indent, pad + indent);
  }

  result += left_padding + "}\n";
  return result;
}
bool StmtBlock::ReplaceChild(AstNode *child, AstNode *dst) {
  XLS_CHECK(child->IsStmtKind() && dst->IsStmtKind());
  Stmt *converted_child = (Stmt *)child;
  Stmt *converted_dst = (Stmt *)dst;
  for (auto &stmt : stmts_) {
    if (stmt == converted_child) {
      stmt = converted_dst;
      dst->SetParent(this);
      return true;
    }
  }
  return false;
}

std::string ReturnStmt::ToString(uint32_t indent, uint32_t pad) const {
  return std::string(pad, ' ') + "return;\n";
}

std::string IfElseStmt::ToString(uint32_t indent, uint32_t pad) const {
  std::string result = std::string(pad, ' ') + "if(";
  result += condition()->ToString(indent, pad);
  result += ")\n";
  result += then_block()->ToString(indent, pad + indent);
  result += std::string(pad, ' ') + "else\n";
  result += else_block()->ToString(indent, pad + indent);
  return result;
}
bool IfElseStmt::ReplaceChild(AstNode *child, AstNode *dst) {
  XLS_CHECK((child->IsStmtKind() && dst->IsStmtKind()) ||
            (child->IsExprKind() && dst->IsExprKind()));
  if (child->IsExprKind()) {
    Expr *converted_child = (Expr *)child;
    Expr *converted_dst = (Expr *)dst;
    if (cond_ == converted_child) {
      cond_ = converted_dst;
      dst->SetParent(this);
      return true;
    }
  } else if (child->IsStmtKind()) {
    Stmt *converted_child = (Stmt *)child;
    Stmt *converted_dst = (Stmt *)dst;
    if (then_block_ == converted_child) {
      then_block_ = converted_dst;
      dst->SetParent(this);
      return true;
    } else if (else_block_ == converted_child) {
      else_block_ = converted_dst;
      dst->SetParent(this);
      return true;
    }
  }
  return false;
}

std::string IfStmt::ToString(uint32_t indent, uint32_t pad) const {
  std::string result = std::string(pad, ' ') + "if(";
  result += condition()->ToString(indent, pad + indent);
  result += ")\n";
  result += then_block()->ToString(indent, pad + indent);
  return result;
}
bool IfStmt::ReplaceChild(AstNode *child, AstNode *dst) {
  XLS_CHECK((child->IsStmtKind() && dst->IsStmtKind()) ||
            (child->IsExprKind() && dst->IsExprKind()));
  if (child->IsExprKind()) {
    Expr *converted_child = (Expr *)child;
    Expr *converted_dst = (Expr *)dst;
    if (cond_ == converted_child) {
      cond_ = converted_dst;
      dst->SetParent(this);
      return true;
    }
  } else if (child->IsStmtKind()) {
    Stmt *converted_child = (Stmt *)child;
    Stmt *converted_dst = (Stmt *)dst;
    if (then_block_ == converted_child) {
      then_block_ = converted_dst;
      dst->SetParent(this);
      return true;
    }
  }
  return false;
}

std::string AssignStmt::ToString(uint32_t indent, uint32_t pad) const {
  return std::string(pad, ' ') + lhs()->ToString(indent, pad) + " = " +
         rhs()->ToString(indent, pad) + ";\n";
}
bool AssignStmt::ReplaceChild(AstNode *child, AstNode *dst) {
  if (child->IsLvalueKind() && (Lvalue *)child == lhs_ && dst->IsLvalueKind()) {
    lhs_ = (Lvalue *)dst;
    dst->SetParent(this);
    return true;
  }
  if (child->IsExprKind() && (Lvalue *)child == rhs_ && dst->IsExprKind()) {
    rhs_ = (Expr *)dst;
    dst->SetParent(this);
    return true;
  }
  return false;
}

std::string ExprEvalStmt::ToString(uint32_t indent, uint32_t pad) const {
  return std::string(pad, ' ') + expr()->ToString(indent, pad) + ";\n";
}
bool ExprEvalStmt::ReplaceChild(AstNode *child, AstNode *dst) {
  XLS_CHECK(child->IsExprKind() && dst->IsExprKind());
  Expr *converted_child = (Expr *)child;
  Expr *converted_dst = (Expr *)dst;
  if (expr_ == converted_child) {
    expr_ = converted_dst;
    dst->SetParent(this);
    return true;
  }
  return false;
}

std::string Module::ToString(uint32_t indent, uint32_t pad) const {
  return body->ToString(indent, pad);
}

std::string NopStmt::ToString(uint32_t indent, uint32_t pad) const {
  return std::string(pad, ' ') + "nop;\n";
}

std::string LongIntLiteralExpr::ToString(uint32_t indent, uint32_t pad) const {
  std::string result = "({";
  for (auto val : value()) {
    result += std::to_string(val) + ", ";
  }
  result += "} : " + std::to_string(value().size() * 64) + "bits)";
  return result;
}

std::string TypeAnnotation::ToString(uint32_t indent, uint32_t pad) const {
  return absl::StrFormat("%s<%u>", name(), size());
}

std::string ArrIndexExpr::ToString(uint32_t indent, uint32_t pad) const {
  return expr()->ToString(indent, pad) + "[" + std::to_string(idx()) + "]";
}
bool ArrIndexExpr::ReplaceChild(AstNode *child, AstNode *dst) {
  XLS_CHECK(child->IsLvalueKind() && dst->IsLvalueKind());
  Lvalue *converted_child = (Lvalue *)child;
  Lvalue *converted_dst = (Lvalue *)dst;
  if (expr_ == converted_child) {
    expr_ = converted_dst;
    dst->SetParent(this);
    return true;
  }
  return false;
}

bool FieldAccessExpr::ReplaceChild(AstNode *child, AstNode *dst) {
  XLS_CHECK(child->IsLvalueKind() && dst->IsLvalueKind());
  Lvalue *converted_child = (Lvalue *)child;
  Lvalue *converted_dst = (Lvalue *)dst;
  if (converted_child == source_) {
    source_ = converted_dst;
    dst->SetParent(this);
    return true;
  }
  return false;
}

BinaryOpExpr *Module::AddBinaryOpExpr(OpKind op, Expr *lhs, Expr *rhs) {
  elements.emplace_back(std::make_unique<BinaryOpExpr>(op, lhs, rhs, this));
  AstNode *result = elements.back().get();
  lhs->SetParent(result);
  rhs->SetParent(result);
  return (BinaryOpExpr *)result;
}

IntLiteralExpr *
Module::AddIntLiteralExpr(uint64_t value, uint32_t size,
                          absl::optional<absl::string_view> name) {
  elements.emplace_back(
      std::make_unique<IntLiteralExpr>(this, value, size, name));
  return (IntLiteralExpr *)elements.back().get();
}

LongIntLiteralExpr *Module::AddLongIntLiteralExpr(std::vector<uint64_t> value) {
  elements.emplace_back(
      std::make_unique<LongIntLiteralExpr>(std::move(value), this));
  return (LongIntLiteralExpr *)elements.back().get();
}

Stmt *Module::AddStmtCopy(Stmt *stmt) {
  AstNode *parent = stmt->parent();
  auto kind = stmt->GetKind();
  Stmt *result = nullptr;

  switch (kind) {
  case AstNodeKind::kIfStmt: {
    auto casting = (IfStmt *)stmt;
    result = AddIfStmt(casting->condition(), casting->then_block());
    break;
  }
  case AstNodeKind::kIfElseStmt: {
    auto casting = (IfElseStmt *)stmt;
    result = AddIfElseStmt(casting->condition(), casting->then_block(),
                           casting->else_block());
    break;
  }
  case AstNodeKind::kAssignStmt: {
    auto casting = (AssignStmt *)stmt;
    result = AddAssignStmt(casting->lhs(), casting->rhs());
    break;
  }
  case AstNodeKind::kStmtBlock: {
    auto casting = (StmtBlock *)stmt;
    result = AddStmtBlock(std::string(casting->name()), casting->stmts());
    break;
  }
  case AstNodeKind::kExprEvalStmt: {
    auto casting = (ExprEvalStmt *)stmt;
    result = AddExprEvalStmt(casting->expr());
    break;
  }
  case AstNodeKind::kReturnStmt: {
    result = AddReturnStmt();
    break;
  }
  case AstNodeKind::kNopStmt: {
    result = AddNopStmt();
    break;
  }
  default:
    XLS_LOG(FATAL) << absl::StrCat(
        "unsupported Stmt type in StmtBlock, AstNode content : ",
        stmt->ToString(2, 0));
  }
  XLS_CHECK(result->GetKind() == kind);
  result->SetParent(parent);
  return result;
}

BuiltinCallExpr *Module::AddBuiltinCallExpr(const std::string &callee,
                                            const std::vector<Expr *> &args) {
  elements.emplace_back(std::make_unique<BuiltinCallExpr>(callee, args, this));
  AstNode *result = elements.back().get();
  for (auto arg : args) {
    arg->SetParent(result);
  }
  return (BuiltinCallExpr *)result;
}

CastExpr *Module::AddCastExpr(Expr *expr, TypeAnnotation *cast_to) {
  elements.emplace_back(std::make_unique<CastExpr>(expr, cast_to, this));
  AstNode *result = elements.back().get();
  expr->SetParent(result);
  cast_to->SetParent(result);
  return (CastExpr *)result;
}

ArrIndexExpr *Module::AddArrIndexExpr(Lvalue *expr, uint32_t idx) {
  elements.emplace_back(std::make_unique<ArrIndexExpr>(expr, idx, this));
  AstNode *result = elements.back().get();
  expr->SetParent(result);
  return (ArrIndexExpr *)result;
}

NameRefExpr *Module::AddNameRefExpr(const std::string &name) {
  elements.emplace_back(std::make_unique<NameRefExpr>(name, this));
  AstNode *result = elements.back().get();
  return (NameRefExpr *)result;
}

FieldAccessExpr *Module::AddFieldAccessExpr(Lvalue *source,
                                            const std::string &field_name) {
  elements.emplace_back(
      std::make_unique<FieldAccessExpr>(source, field_name, this));
  AstNode *result = elements.back().get();
  source->SetParent(result);
  return (FieldAccessExpr *)result;
}

BitSliceExpr *Module::AddBitSliceExpr(Expr *target, uint32_t max_bit,
                                      uint32_t min_bit) {
  elements.emplace_back(
      std::make_unique<BitSliceExpr>(target, max_bit, min_bit, this));
  AstNode *result = elements.back().get();
  target->SetParent(result);
  return (BitSliceExpr *)result;
}

StmtBlock *Module::AddStmtBlock(const std::string &name,
                                const std::vector<Stmt *> &stmts) {
  elements.emplace_back(std::make_unique<StmtBlock>(name, stmts, this));
  AstNode *result = elements.back().get();
  for (auto stmt : stmts) {
    stmt->SetParent(result);
  }
  return (StmtBlock *)result;
}

ReturnStmt *Module::AddReturnStmt() {
  elements.emplace_back(std::make_unique<ReturnStmt>(this));
  AstNode *result = elements.back().get();
  return (ReturnStmt *)result;
}

NopStmt *Module::AddNopStmt() {
  elements.emplace_back(std::make_unique<NopStmt>(this));
  AstNode *result = elements.back().get();
  return (NopStmt *)result;
}

AssignStmt *Module::AddAssignStmt(Lvalue *lhs, Expr *rhs) {
  elements.emplace_back(std::make_unique<AssignStmt>(lhs, rhs, this));
  AstNode *result = elements.back().get();
  lhs->SetParent(result);
  rhs->SetParent(result);
  return (AssignStmt *)result;
}

IfElseStmt *Module::AddIfElseStmt(Expr *cond, Stmt *then_block,
                                  Stmt *else_block) {
  elements.emplace_back(
      std::make_unique<IfElseStmt>(cond, then_block, else_block, this));
  AstNode *result = elements.back().get();

  cond->SetParent(result);
  then_block->SetParent(result);
  else_block->SetParent(result);

  return (IfElseStmt *)result;
}

IfStmt *Module::AddIfStmt(Expr *cond, Stmt *then_block) {
  elements.emplace_back(std::make_unique<IfStmt>(cond, then_block, this));
  AstNode *result = elements.back().get();

  cond->SetParent(result);
  then_block->SetParent(result);

  return (IfStmt *)result;
}

ExprEvalStmt *Module::AddExprEvalStmt(Expr *expr_to_eval) {
  elements.emplace_back(std::make_unique<ExprEvalStmt>(expr_to_eval, this));
  AstNode *result = elements.back().get();
  expr_to_eval->SetParent(result);
  return (ExprEvalStmt *)result;
}

UnaryOpExpr *Module::AddUnaryOpExpr(OpKind op, Expr *operand) {
  elements.emplace_back(std::make_unique<UnaryOpExpr>(op, operand, this));
  AstNode *result = elements.back().get();
  operand->SetParent(result);
  return (UnaryOpExpr *)result;
}

TypeAnnotation *Module::AddTypeAnnotation(uint32_t size,
                                          absl::string_view name) {
  elements.emplace_back(std::make_unique<TypeAnnotation>(size, name, this));
  return (TypeAnnotation *)elements.back().get();
}

absl::Status AstNodeVisitorWithDefault::VisitStmt(Stmt *stmt) {
  switch (stmt->GetKind()) {
  case AstNodeKind::kIfStmt:
    return VisitIfStmt((IfStmt *)stmt);
  case AstNodeKind::kIfElseStmt:
    return VisitIfElseStmt((IfElseStmt *)stmt);
  case AstNodeKind::kAssignStmt:
    return VisitAssignStmt((AssignStmt *)stmt);
  case AstNodeKind::kStmtBlock:
    return VisitStmtBlock((StmtBlock *)stmt);
  case AstNodeKind::kExprEvalStmt:
    return VisitExprEvalStmt((ExprEvalStmt *)stmt);
  case AstNodeKind::kReturnStmt:
    return VisitReturnStmt((ReturnStmt *)stmt);
  case AstNodeKind::kNopStmt:
    return VisitNopStmt((NopStmt *)stmt);
  default:
    return absl::InvalidArgumentError(
        absl::StrCat("unsupported Stmt type in StmtBlock, AstNode content : ",
                     stmt->ToString(2, 0)));
  }
}

absl::Status AstNodeVisitorWithDefault::VisitExpr(Expr *expr) {
  switch (expr->GetKind()) {
  case AstNodeKind::kFieldAccessExpr:
  case AstNodeKind::kBitSliceExpr:
  case AstNodeKind::kNameRefExpr:
  case AstNodeKind::kArrIndexExpr:
  case AstNodeKind::kVarRefExpr:
    return VisitLvalue((Lvalue *)expr);
  case AstNodeKind::kCastExpr:
    return VisitCastExpr((CastExpr *)expr);
  case AstNodeKind::kUnaryOpExpr:
    return VisitUnaryOpExpr((UnaryOpExpr *)expr);
  case AstNodeKind::kBinaryOpExpr:
    return VisitBinaryOpExpr((BinaryOpExpr *)expr);
  case AstNodeKind::kBuiltinCallExpr:
    return VisitBuiltinCallExpr((BuiltinCallExpr *)expr);
  case AstNodeKind::kIntLiteralExpr:
    return VisitIntLiteralExpr((IntLiteralExpr *)expr);
  case AstNodeKind::kLongIntLiteralExpr:
    return VisitLongIntLiteralExpr((LongIntLiteralExpr *)expr);
  default:
    return absl::InvalidArgumentError(
        absl::StrCat("unsupported \"Expr\" type in, AstNode content : ",
                     expr->ToString(2, 0)));
  }
}

absl::Status AstNodeVisitorWithDefault::VisitLvalue(Lvalue *expr) {
  switch (expr->GetKind()) {
  case AstNodeKind::kFieldAccessExpr:
    return VisitFieldAccessExpr((FieldAccessExpr *)expr);
  case AstNodeKind::kNameRefExpr:
    return VisitNameRefExpr((NameRefExpr *)expr);
  case AstNodeKind::kArrIndexExpr:
    return VisitArrIndexExpr((ArrIndexExpr *)expr);
  case AstNodeKind::kBitSliceExpr:
    return VisitBitSliceExpr((BitSliceExpr *)expr);
  case AstNodeKind::kVarRefExpr:
    return VisitVarRefExpr((VarRefExpr *)expr);
  default:
    return absl::InvalidArgumentError(
        absl::StrCat("unsupported \"Lvalue\" type, AstNode content : ",
                     expr->ToString(2, 0)));
  }
}

std::string FakeVarDef::ToString(uint32_t indent, uint32_t pad) const {
  return name();
}

bool FakeVarDef::TryUpdateSize(uint32_t new_size) {
  bool updated = false;
  if (size().has_value()) {
    if (new_size < size().value()) {
      XLS_LOG(WARNING) << "Size narrowing is not allowed";
      return false;
    }
    updated = (new_size != size().value());
  } else {
    updated = true;
  }
  size_ = new_size;
  return updated;
}

std::string VarRefExpr::ToString(uint32_t indent, uint32_t pad) const {
  return def_->ToString(indent, pad);
}

bool VarRefExpr::ReplaceChild(AstNode *child, AstNode *dst) {
  XLS_CHECK(child->GetKind() == AstNodeKind::kFakeVarDef &&
            dst->GetKind() == AstNodeKind::kFakeVarDef);
  FakeVarDef *converted_child = (FakeVarDef *)child;
  FakeVarDef *converted_dst = (FakeVarDef *)dst;
  if (converted_child == def_) {
    def_ = converted_dst;
    dst->SetParent(this);
  }
  return false;
}

FakeVarDef *Module::AddFakeVarDef(std::string name,
                                  absl::optional<uint64_t> size) {
  if (name2def.find(name) != name2def.end()) { // name2def.contains(name)
    XLS_LOG(INFO) << "Module::AddFakeVarDef(), already contains global "
                     "var def, name : "
                  << name << ", directly return" << std::endl;
    FakeVarDef *def = name2def[name];
    if (size.has_value()) {
      def->TryUpdateSize(size.value());
    }
    return def;
  }

  elements.emplace_back(std::make_unique<FakeVarDef>(name, size, this));
  auto var_def = (FakeVarDef *)elements.back().get();

  name2def[name] = var_def;
  names.insert(name);

  return var_def;
}

VarRefExpr *Module::AddVarRefExpr(FakeVarDef *def) {
  elements.emplace_back(std::make_unique<VarRefExpr>(def, this));
  auto var_ref = (VarRefExpr *)elements.back().get();

  return var_ref;
}

bool Module::ReplaceChild(AstNode *child, AstNode *dst) {
  if (child->GetKind() == dst->GetKind() &&
      dst->GetKind() == AstNodeKind::kStmtBlock && body == (StmtBlock *)child) {
    body = (StmtBlock *)dst;
    dst->SetParent(this);
    return true;
  }
  return false;
}

namespace to_json {
nlohmann::json CreateIntLiteral(uint64_t value) {
  auto result = nlohmann::json::object();
  result["TYNAME"] = "INT_LIT";

  std::string str = std::to_string(value);
  result["INT"] = std::string(str.rbegin(), str.rend());
  return result;
}

nlohmann::json CreateIdentifier(const std::string &str) {
  auto result = nlohmann::json::object();
  result["TYNAME"] = "IDENT";
  result["STRING"] = str;
  return result;
}
} // namespace to_json

nlohmann::json FakeVarDef::ToJson() const {
  auto result = to_json::CreateIdentifier(name());
  if (size() != absl::nullopt) {
    result["SIZE"] = *size();
    result["GLOBAL"] = (int)is_global();
  }
  return result;
}

nlohmann::json TypeAnnotation::ToJson() const {
  auto result = to_json::CreateIdentifier(std::string(name()));
  result["TYPESIZE"] = size();
  return result;
}

nlohmann::json BinaryOpExpr::ToJson() const {
  auto result = nlohmann::json::object();
  result["TYNAME"] = OpKindToString(GetOpKind());
  result["OP0"] = lhs()->ToJson();
  result["OP1"] = rhs()->ToJson();
  return result;
}

nlohmann::json UnaryOpExpr::ToJson() const {
  auto result = nlohmann::json::object();
  result["TYNAME"] = OpKindToString(GetOpKind());
  result["OP0"] = operand()->ToJson();
  return result;
}

nlohmann::json IntLiteralExpr::ToJson() const {
  return to_json::CreateIntLiteral(value_);
}

nlohmann::json LongIntLiteralExpr::ToJson() const {
  auto result = nlohmann::json::object();
  result["TYNAME"] = "LIST";
  result["VALUES"] = nlohmann::json::array();

  auto &buffer = result["VALUES"];
  for (uint64_t v : value()) {
    buffer.push_back(to_json::CreateIntLiteral(v));
  }
  return result;
}

nlohmann::json BuiltinCallExpr::ToJson() const {
  auto result = nlohmann::json::object();
  result["TYNAME"] = "FUNCTION_CALL";

  result["OP0"] = to_json::CreateIdentifier(callee());

  result["OP1"] = nlohmann::json::object();
  result["OP1"]["TYNAME"] = "LIST";
  result["OP1"]["VALUES"] = nlohmann::json::array();

  auto &buffer = result["OP1"]["VALUES"];
  for (Expr *arg : args()) {
    buffer.push_back(arg->ToJson());
  }
  return result;
}

nlohmann::json CastExpr::ToJson() const {
  auto result = nlohmann::json::object();
  result["TYNAME"] = "CAST";
  result["OP0"] = expr_to_cast()->ToJson();
  result["OP1"] = cast_to()->ToJson();
  return result;
}

nlohmann::json ArrIndexExpr::ToJson() const {
  auto result = nlohmann::json::object();
  result["TYNAME"] = "INDEX";
  result["OP0"] = expr()->ToJson();
  result["OP1"] = to_json::CreateIntLiteral(idx());
  return result;
}

nlohmann::json NameRefExpr::ToJson() const {
  auto result = to_json::CreateIdentifier(name());
  if (annotation() != absl::nullopt) {
    result["SIZE"] = annotation()->size;
    result["GLOBAL"] = (int)(annotation()->is_global);
  }
  return result;
}

nlohmann::json FieldAccessExpr::ToJson() const {
  auto result = nlohmann::json::object();
  result["TYNAME"] = "DOT";
  result["OP0"] = source()->ToJson();
  result["OP1"] = to_json::CreateIdentifier(std::string(field_name()));

  if (annotation() != absl::nullopt) {
    result["SIZE"] = annotation()->size;
    result["GLOBAL"] = (int)(annotation()->is_global);
    result["STRUCT"] = annotation()->struct_var_name;
    result["OFFSET"] = (int)(annotation()->offset);
  }
  return result;
}

nlohmann::json BitSliceExpr::ToJson() const {
  auto result = nlohmann::json::object();
  result["TYNAME"] = "SLICE";
  result["OP0"] = target()->ToJson();
  result["OP1"] = to_json::CreateIntLiteral(max_bit());
  result["OP2"] = to_json::CreateIntLiteral(min_bit());
  return result;
}

nlohmann::json StmtBlock::ToJson() const {
  auto result = nlohmann::json::object();
  result["TYNAME"] = "BLOCK";

  result["OP0"] = to_json::CreateIdentifier(std::string(name()));

  result["OP1"] = nlohmann::json::object();
  result["OP1"]["TYNAME"] = "LIST";
  result["OP1"]["VALUES"] = nlohmann::json::array();

  auto &buffer = result["OP1"]["VALUES"];
  for (Stmt *stmt : stmts()) {
    buffer.push_back(stmt->ToJson());
  }
  return result;
}

} // namespace xls::p5
