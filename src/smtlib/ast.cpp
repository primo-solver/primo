#include "smtlib/ast.hpp"

#include <utility>

namespace primo::smtlib {

SExpr SExpr::make_atom(std::string value) {
  SExpr expr;
  expr.kind = Kind::Atom;
  expr.atom = std::move(value);
  return expr;
}

SExpr SExpr::make_list(std::vector<SExpr> elements) {
  SExpr expr;
  expr.kind = Kind::List;
  expr.list = std::move(elements);
  return expr;
}

bool SExpr::is_atom(const std::string& value) const { return kind == Kind::Atom && atom == value; }

}  // namespace primo::smtlib
