#pragma once

#include <string>
#include <vector>

#include "smtlib/lexer.hpp"

namespace primo::smtlib {

// A generic S-expression tree (atom or list of S-expressions). Deeply
// nested input is common from fuzzers (long `let`/arithmetic chains in
// particular), so copying and destroying an SExpr tree deliberately does
// NOT use the compiler-generated recursive copy constructor/destructor
// (which would recurse one call-stack frame per nesting level and can
// stack-overflow) -- both are hand-written using an explicit stack/work
// queue instead. See ast.cpp for the iterative algorithms. Moving an
// SExpr stays O(1)/non-recursive (defaulted), since a move only transfers
// buffer ownership, never walks the tree.
struct SExpr {
  enum class Kind {
    Atom,
    List,
  };

  Kind kind = Kind::Atom;
  TokenKind atom_kind = TokenKind::Symbol;
  // Canonical interior for symbols; original spelling for other token kinds.
  std::string atom;
  std::vector<SExpr> list;

  SExpr() = default;
  SExpr(const SExpr& other);
  SExpr& operator=(const SExpr& other);
  SExpr(SExpr&&) noexcept = default;
  SExpr& operator=(SExpr&&) noexcept = default;
  ~SExpr();

  static SExpr make_atom(std::string value, TokenKind category = TokenKind::Symbol);
  bool is_symbol() const;
  static SExpr make_list(std::vector<SExpr> elements);
  bool is_atom(const std::string& value) const;
};

}  // namespace primo::smtlib
