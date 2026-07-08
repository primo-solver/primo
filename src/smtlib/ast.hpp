#pragma once

#include <string>
#include <vector>

namespace primo::smtlib {

struct SExpr {
  enum class Kind {
    Atom,
    List,
  };

  Kind kind = Kind::Atom;
  std::string atom;
  std::vector<SExpr> list;

  static SExpr make_atom(std::string value);
  static SExpr make_list(std::vector<SExpr> elements);
  bool is_atom(const std::string& value) const;
};

}  // namespace primo::smtlib
