#include "smtlib/attributes.hpp"

#include <cstddef>
#include <span>

#include "smtlib/ast.hpp"
#include "smtlib/lexer.hpp"

namespace primo::smtlib {

bool is_attribute_value(const SExpr& expr) {
  if (expr.kind == SExpr::Kind::List) return true;
  switch (expr.atom_kind) {
    case TokenKind::Symbol:
    case TokenKind::Numeral:
    case TokenKind::Decimal:
    case TokenKind::Binary:
    case TokenKind::Hexadecimal:
    case TokenKind::String:
      return true;
    default:
      return false;
  }
}

bool valid_attributes(std::span<const SExpr> attributes) {
  if (attributes.empty()) return false;
  for (std::size_t i = 0; i < attributes.size();) {
    if (attributes[i].kind != SExpr::Kind::Atom || attributes[i].atom_kind != TokenKind::Keyword)
      return false;
    ++i;
    if (i < attributes.size() && is_attribute_value(attributes[i])) ++i;
  }
  return true;
}

}  // namespace primo::smtlib
