#include "xls/p5/keywords.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace xls::p5 {
bool IsBinaryOperator(OpKind kind) {
#define GEN_CONDITION(_str, _enum) || kind == OpKind::_enum
  return false P5_BINARY_OP(GEN_CONDITION);
#undef GEN_CONDITION
}

bool IsUnaryOperator(OpKind kind) {
#define GEN_CONDITION(_str, _enum) || kind == OpKind::_enum
  return false P5_UNARY_OP(GEN_CONDITION);
#undef GEN_CONDITION
}

absl::StatusOr<OpKind> StrToOpKind(absl::string_view str) {
  if (false) {
    // DO NOTHING, as a dummy branch
  }
#define GEN_BRANCH(_str, _enum)                                                \
  else if (str == _str) {                                                      \
    return OpKind::_enum;                                                      \
  }
  P5_BINARY_OP(GEN_BRANCH)
  P5_UNARY_OP(GEN_BRANCH)
#undef GEN_BRANCH
  else {
    return absl::InvalidArgumentError(absl::StrCat("No such OpKind: ", str));
  }
}

// absl::StatusOr<BuiltInFuncKind> StrToBuiltInFuncKind(absl::string_view str);

std::string OpKindToString(OpKind kind) {
  switch (kind) {
#define GEN_CASE(_str, _enum)                                                  \
  case OpKind::_enum:                                                          \
    return _str;
    P5_BINARY_OP(GEN_CASE) P5_UNARY_OP(GEN_CASE)
#undef GEN_CASE
  }
}

#define PUSH_BACK_STR(_str, _enum) result.push_back(_str);
std::vector<std::string> GetExprTypeNames() {
  std::vector<std::string> result;
  P5_EXPR_OP(PUSH_BACK_STR)
  return result;
}

std::vector<std::string> GetStmtTypeNames() {
  std::vector<std::string> result;
  P5_STMT_OP(PUSH_BACK_STR)
  return result;
}
#undef PUSH_BACK_STR

} // namespace xls::p5