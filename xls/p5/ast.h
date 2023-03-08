#pragma once

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "xls/p5/keywords.h"
#include "xls/p5/util/json.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace xls::p5 {

#define AST_NODE(X)                                                            \
  X(kNameRefExpr, NameRefExpr)                                                 \
  X(kBitSliceExpr, BitSliceExpr)                                               \
  X(kFieldAccessExpr, FieldAccessExpr)                                         \
  X(kBinaryOpExpr, BinaryOpExpr)                                               \
  X(kUnaryOpExpr, UnaryOpExpr)                                                 \
  X(kBuiltinCallExpr, BuiltinCallExpr)                                         \
  X(kIntLiteralExpr, IntLiteralExpr)                                           \
  X(kLongIntLiteralExpr, LongIntLiteralExpr)                                   \
  X(kCastExpr, CastExpr)                                                       \
  X(kArrIndexExpr, ArrIndexExpr)                                               \
  X(kAssignStmt, AssignStmt)                                                   \
  X(kIfElseStmt, IfElseStmt)                                                   \
  X(kIfStmt, IfStmt)                                                           \
  X(kReturnStmt, ReturnStmt)                                                   \
  X(kNopStmt, NopStmt)                                                         \
  X(kExprEvalStmt, ExprEvalStmt)                                               \
  X(kStmtBlock, StmtBlock)                                                     \
  X(kTypeAnnotation, TypeAnnotation)                                           \
  X(kModule, Module)                                                           \
  X(kFakeVarDef, FakeVarDef)                                                   \
  X(kVarRefExpr, VarRefExpr)

enum class AstNodeKind {
#define GEN_ENUM(_enum, _) _enum,
  AST_NODE(GEN_ENUM)
#undef GEN_ENUM
};

#define CLASS_FORWARD_DECL(_, _class_name) class _class_name;
AST_NODE(CLASS_FORWARD_DECL)
#undef CLASS_FORWARD_DECL
class Expr;
class Stmt;
class Lvalue;

class AstNodeVisitor {
public:
  virtual ~AstNodeVisitor() = default;

#define DECL_HANDLER(_, _class_name)                                           \
  virtual absl::Status Visit##_class_name(_class_name *node) = 0;
  AST_NODE(DECL_HANDLER)
#undef DECL_HANDLER

  virtual absl::Status VisitStmt(Stmt *) = 0;
  virtual absl::Status VisitLvalue(Lvalue *) = 0;
  virtual absl::Status VisitExpr(Expr *) = 0;
};

class AstNodeVisitorWithDefault : public AstNodeVisitor {
public:
  ~AstNodeVisitorWithDefault() override = default;

#define DECL_HANDLER(_, _class_name)                                           \
  absl::Status Visit##_class_name(_class_name *node) override {                \
    return absl::OkStatus();                                                   \
  };
  AST_NODE(DECL_HANDLER)
#undef DECL_HANDLER

  absl::Status VisitStmt(Stmt *) override;
  absl::Status VisitLvalue(Lvalue *) override;
  absl::Status VisitExpr(Expr *) override;
};

class VisitAll : public AstNodeVisitorWithDefault {
public:
  virtual absl::Status VisitModule(Module *node) override;
  virtual absl::Status VisitStmtBlock(StmtBlock *node) override;
  virtual absl::Status VisitIfStmt(IfStmt *node) override;
  virtual absl::Status VisitIfElseStmt(IfElseStmt *node) override;
  virtual absl::Status VisitAssignStmt(AssignStmt *node) override;
  virtual absl::Status VisitExprEvalStmt(ExprEvalStmt *node) override;
  virtual absl::Status VisitBuiltinCallExpr(BuiltinCallExpr *node) override;
  virtual absl::Status VisitNameRefExpr(NameRefExpr *node) override;
  virtual absl::Status VisitFieldAccessExpr(FieldAccessExpr *node) override;
  virtual absl::Status VisitArrIndexExpr(ArrIndexExpr *node) override;
  virtual absl::Status VisitBitSliceExpr(BitSliceExpr *node) override;
  virtual absl::Status VisitBinaryOpExpr(BinaryOpExpr *node) override;
  virtual absl::Status VisitUnaryOpExpr(UnaryOpExpr *node) override;
  virtual absl::Status VisitCastExpr(CastExpr *node) override;
  virtual absl::Status VisitIntLiteralExpr(IntLiteralExpr *node) override;
  virtual absl::Status
  VisitLongIntLiteralExpr(LongIntLiteralExpr *node) override;
  virtual absl::Status VisitReturnStmt(ReturnStmt *node) override;
  virtual absl::Status VisitNopStmt(NopStmt *node) override;
  virtual absl::Status VisitTypeAnnotation(TypeAnnotation *node) override;
  virtual absl::Status VisitFakeVarDef(FakeVarDef *node) override;
  virtual absl::Status VisitVarRefExpr(VarRefExpr *node) override;
};

// TODO: Add method: AddDebugInfo, AddAnno
// NOT-IMPORTANT
class AstNode {
public:
  explicit AstNode(AstNodeKind kind, Module *module, AstNode *parent = nullptr)
      : kind_{kind}, parent_(parent), module_(module) {}
  virtual ~AstNode() = default;

  AstNodeKind GetKind() const { return kind_; }

  bool IsStmtKind() const {
    auto kind = GetKind();
    return kind == AstNodeKind::kReturnStmt || kind == AstNodeKind::kNopStmt ||
           kind == AstNodeKind::kExprEvalStmt ||
           kind == AstNodeKind::kStmtBlock ||
           kind == AstNodeKind::kIfElseStmt || kind == AstNodeKind::kIfStmt ||
           kind == AstNodeKind::kAssignStmt;
  }

  bool IsExprKind() const {
    auto kind = GetKind();
    return kind == AstNodeKind::kCastExpr ||
           kind == AstNodeKind::kIntLiteralExpr ||
           kind == AstNodeKind::kBuiltinCallExpr ||
           kind == AstNodeKind::kBinaryOpExpr ||
           kind == AstNodeKind::kUnaryOpExpr ||
           kind == AstNodeKind::kLongIntLiteralExpr || IsLvalueKind();
  }

  bool IsLvalueKind() const {
    auto kind = GetKind();
    return kind == AstNodeKind::kBitSliceExpr ||
           kind == AstNodeKind::kArrIndexExpr ||
           kind == AstNodeKind::kNameRefExpr ||
           kind == AstNodeKind::kFieldAccessExpr ||
           kind == AstNodeKind::kVarRefExpr;
  }

  AstNode *parent() const { return parent_; }

  void SetParent(AstNode *parent);

  // If contains child, and successfuly replaced, then return "true"
  virtual bool ReplaceChild(AstNode *child, AstNode *dst) { return false; }

  virtual absl::Status Accept(AstNodeVisitor *visitor) = 0;

  virtual std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const = 0;

  virtual nlohmann::json ToJson() const = 0;

  Module *module() const { return module_; }

private:
  AstNodeKind kind_;
  AstNode *parent_;
  Module *module_;
};

class FakeVarDef : public AstNode {
public:
  explicit FakeVarDef(const std::string &name, absl::optional<uint32_t> size,
                      Module *module)
      : AstNode(AstNodeKind::kFakeVarDef, module), name_(name), size_(size) {}

  const std::string &name() const { return name_; }

  absl::optional<uint32_t> size() const { return size_; }

  void SetSize(uint32_t size) { size_ = size; }

  bool is_global() const { return is_global_; }

  void SetIsGlobal(bool is_global) { is_global_ = is_global; }

  // Update variable's size
  //   only widening is acceptable, or size has no value,
  //   narrowing will be ignored,
  //   return true if `size` is successfully updated
  bool TryUpdateSize(uint32_t new_size);

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  nlohmann::json ToJson() const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitFakeVarDef(this);
  }

private:
  std::string name_;
  bool is_global_ = true; // by default, everything is global
  absl::optional<uint32_t> size_ = absl::nullopt;
};

class TypeAnnotation : public AstNode {
public:
  TypeAnnotation(uint32_t size, absl::string_view name, Module *module)
      : AstNode(AstNodeKind::kTypeAnnotation, module), size_(size),
        name_(name) {}

  uint32_t size() const { return size_; }

  absl::string_view name() const { return name_; }

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitTypeAnnotation(this);
  }

  nlohmann::json ToJson() const override;

private:
  uint32_t size_;
  std::string name_;
};

class Expr : public AstNode {
public:
  explicit Expr(AstNodeKind kind, Module *module) : AstNode(kind, module) {}
};

class BinaryOpExpr : public Expr {
public:
  BinaryOpExpr(OpKind op, Expr *lhs, Expr *rhs, Module *module);

  OpKind GetOpKind() const { return op_; }

  Expr *lhs() const { return lhs_; }

  Expr *rhs() const { return rhs_; }

  bool ReplaceChild(AstNode *child, AstNode *dst) override;

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitBinaryOpExpr(this);
  }

  nlohmann::json ToJson() const override;

private:
  OpKind op_;
  Expr *lhs_;
  Expr *rhs_;
};

class UnaryOpExpr : public Expr {
public:
  UnaryOpExpr(OpKind op, Expr *operand, Module *module);

  OpKind GetOpKind() const { return op_; }

  Expr *operand() const { return operand_; }

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  bool ReplaceChild(AstNode *child, AstNode *dst) override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitUnaryOpExpr(this);
  }

  nlohmann::json ToJson() const override;

private:
  OpKind op_;
  Expr *operand_;
};

class IntLiteralExpr : public Expr {
public:
  using value_type = uint64_t;
  using size_type = uint32_t;

  explicit IntLiteralExpr(Module *module, value_type value, size_type size,
                          absl::optional<absl::string_view> name)
      : Expr(AstNodeKind::kIntLiteralExpr, module), value_(value), size_(size) {
    if (name) {
      name_ = std::string(name.value());
    } else {
      name_ = absl::nullopt;
    }
  }

  value_type value() const { return value_; }

  size_type size() const { return size_; }

  absl::optional<absl::string_view> name() const { return name_; }

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitIntLiteralExpr(this);
  }
  nlohmann::json ToJson() const override;

private:
  value_type value_;
  size_type size_;
  absl::optional<std::string> name_;
};

class LongIntLiteralExpr : public Expr {
public:
  explicit LongIntLiteralExpr(std::vector<uint64_t> value, Module *module)
      : Expr(AstNodeKind::kLongIntLiteralExpr, module),
        value_(std::move(value)) {}

  const std::vector<uint64_t> &value() const { return value_; }

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitLongIntLiteralExpr(this);
  }

  nlohmann::json ToJson() const override;

private:
  std::vector<uint64_t> value_;
};

// TODO: using enum to represent callee
// NOT IMPORTANT
class BuiltinCallExpr : public Expr {
public:
  BuiltinCallExpr(std::string callee, std::vector<Expr *> args, Module *module)
      : Expr(AstNodeKind::kBuiltinCallExpr, module), callee_(std::move(callee)),
        args_(std::move(args)) {}

  const std::string &callee() const { return callee_; }

  const std::vector<Expr *> &args() const { return args_; }

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  bool ReplaceChild(AstNode *child, AstNode *dst) override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitBuiltinCallExpr(this);
  }

  nlohmann::json ToJson() const override;

private:
  std::string callee_;
  std::vector<Expr *> args_;
};

class CastExpr : public Expr {
public:
  CastExpr(Expr *expr, TypeAnnotation *cast_to, Module *module)
      : Expr(AstNodeKind::kCastExpr, module),
        expr_to_cast_{expr}, cast_to_{cast_to} {}

  Expr *expr_to_cast() const { return expr_to_cast_; }

  TypeAnnotation *cast_to() const { return cast_to_; }

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitCastExpr(this);
  }

  bool ReplaceChild(AstNode *child, AstNode *dst) override;

  nlohmann::json ToJson() const override;

private:
  Expr *expr_to_cast_;
  TypeAnnotation *cast_to_;
};

class Lvalue : public Expr {
public:
  explicit Lvalue(AstNodeKind kind, Module *module) : Expr(kind, module) {}
};

class ArrIndexExpr : public Lvalue {
public:
  ArrIndexExpr(Lvalue *expr, uint32_t idx, Module *module)
      : Lvalue(AstNodeKind::kArrIndexExpr, module), expr_(expr), idx_(idx) {}

  Lvalue *expr() const { return expr_; }

  uint32_t idx() const { return idx_; }

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  bool ReplaceChild(AstNode *child, AstNode *dst) override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitArrIndexExpr(this);
  }

  nlohmann::json ToJson() const override;

private:
  Lvalue *expr_;
  uint32_t idx_;
};

// TODO: change lowering process, do not need to deal with true/false
// TODO: add passes to extract type-info
class NameRefExpr : public Lvalue {
public:
  using size_type = uint32_t;
  using value_type = uint64_t;

  struct Annotation {
    Annotation(size_type size_, bool is_global_)
        : size(size_), is_global(is_global_) {}
    Annotation(const Annotation &) = default;

    size_type size;
    bool is_global;
  };

  explicit NameRefExpr(const std::string &name, Module *module)
      : Lvalue(AstNodeKind::kNameRefExpr, module), name_(name) {}

  const std::string &name() const { return name_; }

  NameRefExpr &AddAnnotation(const Annotation &annotation) {
    annotation_ = annotation;
    return *this;
  }

  absl::optional<Annotation> annotation() const { return annotation_; }

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitNameRefExpr(this);
  }

  nlohmann::json ToJson() const override;

private:
  std::string name_;
  absl::optional<Annotation> annotation_ = absl::nullopt;
};

class VarRefExpr : public Lvalue {
public:
  explicit VarRefExpr(FakeVarDef *def, Module *module)
      : Lvalue(AstNodeKind::kVarRefExpr, module), def_(def) {}

  FakeVarDef *def() const { return def_; }

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitVarRefExpr(this);
  }

  bool ReplaceChild(AstNode *child, AstNode *dst) override;

  nlohmann::json ToJson() const override { return def_->ToJson(); }

private:
  FakeVarDef *def_;
};

// TODO: add passes to extract type-info
class FieldAccessExpr : public Lvalue {
public:
  struct Annotation {
    Annotation(uint32_t size_, bool is_global_,
               absl::string_view struct_var_name_, uint32_t offset_)
        : size(size_), is_global(is_global_), struct_var_name(struct_var_name_),
          offset(offset_) {}

    Annotation(const Annotation &) = default;

    uint32_t size;
    bool is_global;
    std::string struct_var_name;
    uint32_t offset;
  };

  FieldAccessExpr(Lvalue *source, const std::string &field_name, Module *module)
      : Lvalue(AstNodeKind::kFieldAccessExpr, module), source_(source),
        field_name_(field_name) {}

  std::string_view field_name() const { return field_name_; }

  Lvalue *source() const { return source_; }

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitFieldAccessExpr(this);
  }

  bool ReplaceChild(AstNode *child, AstNode *dst) override;

  FieldAccessExpr &AddAnnotation(const Annotation &info) {
    type_info_ = info;
    return *this;
  }

  absl::optional<Annotation> annotation() const { return type_info_; }

  nlohmann::json ToJson() const override;

private:
  Lvalue *source_;
  std::string field_name_;
  absl::optional<Annotation> type_info_ = absl::nullopt;
};

class BitSliceExpr : public Lvalue {
public:
  BitSliceExpr(Expr *target, uint32_t max_bit, uint32_t min_bit, Module *module)
      : Lvalue(AstNodeKind::kBitSliceExpr, module), target_(target),
        min_bit_(min_bit), max_bit_(max_bit) {}

  Expr *target() const { return target_; }

  uint32_t max_bit() const { return max_bit_; }

  uint32_t min_bit() const { return min_bit_; }

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitBitSliceExpr(this);
  }

  bool ReplaceChild(AstNode *child, AstNode *dst) override;

  nlohmann::json ToJson() const override;

private:
  Expr *target_;
  uint32_t min_bit_;
  uint32_t max_bit_;
};

class Stmt : public AstNode {
public:
  Stmt(AstNodeKind kind, Module *module, Stmt *parent)
      : AstNode(kind, module, parent) {}
  explicit Stmt(AstNodeKind kind, Module *module) : AstNode(kind, module) {}

  virtual uint64_t CountActiveStmt() const { return 1; }
};

class StmtBlock : public Stmt {
public:
  StmtBlock(const std::string &name, Module *module, Stmt *parent)
      : Stmt(AstNodeKind::kStmtBlock, module, parent), name_(name) {}

  StmtBlock(const std::string &name, std::vector<Stmt *> stmts, Module *module)
      : Stmt(AstNodeKind::kStmtBlock, module), name_(name),
        stmts_(std::move(stmts)) {}

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  inline void Add(Stmt *stmt) { stmts_.emplace_back(stmt); }

  const std::vector<Stmt *> &stmts() const { return stmts_; }

  void ReplaceAll(const std::vector<Stmt *> &stmts);

  std::string_view name() const { return name_; }

  bool empty() const { return stmts_.empty(); }

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitStmtBlock(this);
  }

  bool ReplaceChild(AstNode *child, AstNode *dst) override;

  nlohmann::json ToJson() const override;

  uint64_t CountActiveStmt() const override {
    uint64_t result = 1;
    for (Stmt *stmt : stmts()) {
      result += stmt->CountActiveStmt();
    }
    return result;
  }

private:
  std::string name_;
  std::vector<Stmt *> stmts_;
};

class ReturnStmt : public Stmt {
public:
  ReturnStmt(Module *module, Stmt *parent)
      : Stmt(AstNodeKind::kReturnStmt, module, parent) {}
  ReturnStmt(Module *module) : Stmt(AstNodeKind::kReturnStmt, module) {}

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitReturnStmt(this);
  }

  nlohmann::json ToJson() const override {
    auto result = nlohmann::json::object();
    result["TYNAME"] = "RETURN";
    return result;
  }
};

class NopStmt : public Stmt {
public:
  NopStmt(Module *module, Stmt *parent)
      : Stmt(AstNodeKind::kNopStmt, module, parent) {}
  NopStmt(Module *module) : Stmt(AstNodeKind::kNopStmt, module) {}

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitNopStmt(this);
  }

  nlohmann::json ToJson() const override {
    auto result = nlohmann::json::object();
    result["TYNAME"] = "NOP";
    return result;
  }
};

class IfElseStmt : public Stmt {
public:
  IfElseStmt(Expr *cond, Stmt *then_block, Stmt *else_block, Module *module,
             Stmt *parent)
      : Stmt(AstNodeKind::kIfElseStmt, module, parent), cond_(cond),
        then_block_(then_block), else_block_(else_block) {}

  IfElseStmt(Expr *cond, Stmt *then_block, Stmt *else_block, Module *module)
      : Stmt(AstNodeKind::kIfElseStmt, module), cond_(cond),
        then_block_(then_block), else_block_(else_block) {}

  Expr *condition() const { return cond_; }

  Stmt *then_block() const { return then_block_; }

  Stmt *else_block() const { return else_block_; }

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitIfElseStmt(this);
  }

  bool ReplaceChild(AstNode *child, AstNode *dst) override;

  nlohmann::json ToJson() const override {
    auto result = nlohmann::json::object();
    result["TYNAME"] = "IF";
    result["OP0"] = condition()->ToJson();
    result["OP1"] = then_block()->ToJson();
    result["OP2"] = else_block()->ToJson();
    return result;
  }

  uint64_t CountActiveStmt() const override {
    return 1 + then_block()->CountActiveStmt() + else_block()->CountActiveStmt();
  }

private:
  Expr *cond_;
  Stmt *then_block_;
  Stmt *else_block_;
};

class IfStmt : public Stmt {
public:
  IfStmt(Expr *cond, Stmt *then_block, Module *module, Stmt *parent)
      : Stmt(AstNodeKind::kIfStmt, module, parent), cond_(cond),
        then_block_(then_block) {}

  IfStmt(Expr *cond, Stmt *then_block, Module *module)
      : Stmt(AstNodeKind::kIfStmt, module), cond_(cond),
        then_block_(then_block) {}

  Expr *condition() const { return cond_; }

  Stmt *then_block() const { return then_block_; }

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitIfStmt(this);
  }

  bool ReplaceChild(AstNode *child, AstNode *dst) override;

  nlohmann::json ToJson() const override {
    auto result = nlohmann::json::object();
    result["TYNAME"] = "IF";
    result["OP0"] = condition()->ToJson();
    result["OP1"] = then_block()->ToJson();
    return result;
  }

  uint64_t CountActiveStmt() const override {
    return 1 + then_block()->CountActiveStmt();
  }

private:
  Expr *cond_;
  Stmt *then_block_;
};

class AssignStmt : public Stmt {
public:
  AssignStmt(Lvalue *lhs, Expr *rhs, Module *module, Stmt *parent)
      : Stmt(AstNodeKind::kAssignStmt, module, parent), lhs_(lhs), rhs_(rhs) {}

  AssignStmt(Lvalue *lhs, Expr *rhs, Module *module)
      : Stmt(AstNodeKind::kAssignStmt, module), lhs_(lhs), rhs_(rhs) {}

  Lvalue *lhs() const { return lhs_; }

  Expr *rhs() const { return rhs_; }

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitAssignStmt(this);
  }

  bool ReplaceChild(AstNode *child, AstNode *dst) override;

  nlohmann::json ToJson() const override {
    auto result = nlohmann::json::object();
    result["TYNAME"] = "ASSIGN";
    result["OP0"] = lhs()->ToJson();
    result["OP1"] = rhs()->ToJson();
    return result;
  }

private:
  Lvalue *lhs_;
  Expr *rhs_;
};

class ExprEvalStmt : public Stmt {
public:
  ExprEvalStmt(Expr *expr_to_eval, Module *module, Stmt *parent)
      : Stmt(AstNodeKind::kExprEvalStmt, module, parent), expr_(expr_to_eval) {}

  ExprEvalStmt(Expr *expr_to_eval, Module *module)
      : Stmt(AstNodeKind::kExprEvalStmt, module), expr_(expr_to_eval) {}

  Expr *expr() const { return expr_; }

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitExprEvalStmt(this);
  }
  bool ReplaceChild(AstNode *child, AstNode *dst) override;

  nlohmann::json ToJson() const override { return expr()->ToJson(); }

private:
  Expr *expr_;
};

class Module : public AstNode {
public:
  Module() : AstNode(AstNodeKind::kModule, this) {}

  std::string ToString(uint32_t indent = 2, uint32_t pad = 0) const override;

  absl::Status Accept(AstNodeVisitor *visitor) override {
    return visitor->VisitModule(this);
  }

  bool ReplaceChild(AstNode *child, AstNode *dst) override;

  BinaryOpExpr *AddBinaryOpExpr(OpKind op, Expr *lhs, Expr *rhs);

  UnaryOpExpr *AddUnaryOpExpr(OpKind op, Expr *operand);

  IntLiteralExpr *
  AddIntLiteralExpr(uint64_t value, uint32_t size,
                    absl::optional<absl::string_view> name = absl::nullopt);

  LongIntLiteralExpr *AddLongIntLiteralExpr(std::vector<uint64_t> value);

  BuiltinCallExpr *AddBuiltinCallExpr(const std::string &callee,
                                      const std::vector<Expr *> &args);

  CastExpr *AddCastExpr(Expr *expr, TypeAnnotation *cast_to);

  ArrIndexExpr *AddArrIndexExpr(Lvalue *expr, uint32_t idx);

  NameRefExpr *AddNameRefExpr(const std::string &name);

  FieldAccessExpr *AddFieldAccessExpr(Lvalue *source,
                                      const std::string &field_name);

  BitSliceExpr *AddBitSliceExpr(Expr *target, uint32_t max_bit,
                                uint32_t min_bit);

  StmtBlock *AddStmtBlock(const std::string &name,
                          const std::vector<Stmt *> &stmts);

  ReturnStmt *AddReturnStmt();

  NopStmt *AddNopStmt();

  IfElseStmt *AddIfElseStmt(Expr *cond, Stmt *then_block, Stmt *else_block);

  IfStmt *AddIfStmt(Expr *cond, Stmt *then_block);

  AssignStmt *AddAssignStmt(Lvalue *lhs, Expr *rhs);

  ExprEvalStmt *AddExprEvalStmt(Expr *expr_to_eval);

  TypeAnnotation *AddTypeAnnotation(uint32_t size, absl::string_view name);

  FakeVarDef *AddFakeVarDef(std::string name,
                            absl::optional<uint64_t> size = absl::nullopt);

  VarRefExpr *AddVarRefExpr(FakeVarDef *def);

  Stmt *AddStmtCopy(Stmt *stmt);

  nlohmann::json ToJson() const override { return body->ToJson(); }

  StmtBlock *body;
  std::map<std::string, FakeVarDef *> name2def;
  std::set<std::string> names;
  // std::vector<std::unique_ptr<FakeGlobalVariableDecl>> global_variables_;
  std::vector<std::unique_ptr<AstNode>> elements;
};

} // namespace xls::p5
