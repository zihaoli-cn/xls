#pragma once

#include "absl/status/statusor.h"
#include <string>
namespace xls::p5 {
#define P5_BINARY_OP(X)                                                        \
  /* logical operator */                                                       \
  X("LAND", kLogicalAnd)                                                       \
  X("LOR", kLogicalOr)                                                         \
  /* relationship operator */                                                  \
  X("EQ", kEqual)                                                              \
  X("NE", kNotEqual)                                                           \
  X("LE", kLessEqual)                                                          \
  X("LT", kLessThan)                                                           \
  X("GE", kGreaterEqual)                                                       \
  X("GT", kGreaterThan)                                                        \
  /* bitwise operator */                                                       \
  X("AND", kBitwiseAnd)                                                        \
  X("OR", kBitwiseOr)                                                          \
  X("LSHIFT", kLeftShift)                                                      \
  X("RSHIFT", kRightShift)                                                     \
  /* arithmetic operator */                                                    \
  X("PLUS", kPlus)                                                             \
  X("MINUS", kMinus)                                                           \
  X("MUL", kMul)                                                               \
  X("DIVIDE", kDiv)

#define P5_UNARY_OP(X)                                                         \
  /* logical operator */                                                       \
  X("LNOT", kLogicalNot)                                                       \
  /* bitwise operator */                                                       \
  X("NOT", kBitwiseNot)

#define P5_AST_JSON_TYNAME(X)                                                  \
  X("LIST", kList)                                                             \
  X("ANNOTATION", kAnnotation)                                                 \
  X("FUNCTION_CALL", kFunctionCall)                                            \
  X("IDENT", kIdentifier)                                                      \
  X("CAST", kCast)                                                             \
  X("DOT", kDot)                                                               \
  X("INDEX", kArrayIndex)                                                      \
  X("INT_LIT", kIntLiteral)                                                    \
  X("SLICE", kSlice)                                                           \
  X("ASSIGN", kAssign)                                                         \
  X("BLOCK", kBlock)                                                           \
  X("IF", kIf)                                                                 \
  X("RETURN", kReturn)                                                         \
  X("NOP", kNop)                                                               \
  P5_BINARY_OP(X)                                                              \
  P5_UNARY_OP(X)

#define P5_BUILT_IN_FUNCTION(X)                                                \
  X("sizeof", kSizeOf)                                                         \
  X("_get_anchor", kGetAnchor)                                                 \
  X("_stack_push_b", kStackPushB)                                              \
  X("_stack_push_h", kStackPushH)                                              \
  X("_valid_set", kValidSet)                                                   \
  X("_valid", kValid)

#define ENUM_COMMA_MACRO(_str, _enum) _enum,

enum class OpKind {
  P5_BINARY_OP(ENUM_COMMA_MACRO) P5_UNARY_OP(ENUM_COMMA_MACRO)
};

// enum class BuiltInFuncKind{
//  P5_BUILT_IN_FUNCTION(ENUM_COMMA_MACRO)
// };

#undef ENUM_COMMA_MACRO

bool IsBinaryOperator(OpKind kind);

bool IsUnaryOperator(OpKind kind);

absl::StatusOr<OpKind> StrToOpKind(absl::string_view str);

// absl::StatusOr<BuiltInFuncKind> StrToBuiltInFuncKind(absl::string_view str);

std::string OpKindToString(OpKind kind);

// std::string ToString(BuiltInFuncKind kind);
} // namespace xls::p5