#pragma once

#include <vector>

#include "core/ids.hpp"
#include "util/rational.hpp"

namespace primo::core {

enum class TermKind {
  BoolValue,
  Constant,
  Application,
  Equality,
  Not,
  And,
  Or,
  Implies,
  // Linear real arithmetic (QF_LRA). Per SMT-LIB QF_LRA's own grammar,
  // `*` only ever appears as a literal-coefficient times a single term
  // (`(* c x)`/`(* x c)`), never two arbitrary terms multiplied together --
  // so Times is a *unary* scale node (rational_value * the one child), not
  // general multiplication, and `/` is resolved into a literal Rational
  // coefficient at parse time rather than becoming a term kind at all.
  // Unary `-` desugars to Times with coefficient -1 (no separate Neg kind,
  // so `(- x)` and `(* -1 x)` hash-cons to the same term); n-ary `+` folds
  // into Plus; `>=`/`>` desugar to flipped Le/Lt.
  Rational,
  Plus,
  Times,
  Le,
  Lt,
  // Structure that used to be destroyed before preprocessing could see it.
  // `ite` was expanded in the frontend with no simplification attempted at
  // all -- not even `(ite c a a)` -- and `div`/`mod`/`abs` were rejected as
  // unknown symbols. They are terms now, and an unconditional preprocessing
  // leaf lowers them; see docs/papers/integer-division-and-modulo.md.
  //
  // New kinds are appended, never inserted: KeyHash casts TermKind to
  // std::size_t, so appending keeps existing hashes stable.
  //
  // Ite carries {condition, then, else} and takes the branch sort, which may
  // be Bool or an uninterpreted sort, not only arithmetic.
  //
  // IntDiv and IntMod follow the Times shape above: one child, the numerator,
  // with the literal divisor in rational_value rather than as a child. A
  // non-literal divisor is nonlinear and is rejected by the frontend. The
  // divisor is normalized positive on construction, which is a representation
  // invariant rather than a simplification: SMT-LIB fixes div and mod by
  // Boute's Euclidean definition, and for a positive divisor that coincides
  // with floored division, so a positive divisor is what lets constant folding
  // use Rational::floor(). The normalization itself is Boute's identity pair
  // divE(D, -d) = -divE(D, d) and modE(D, -d) = modE(D, d).
  Ite,
  IntDiv,
  IntMod,
  Abs,
  // Quantifiers, with de Bruijn bound variables (docs/quantifiers.md).
  //
  // BoundVariable keeps its index in `symbol` -- a SymbolId-sized integer,
  // not a symbol -- and its sort in `sort`. Index 0 is the innermost enclosing
  // bound variable. Only core/binders.hpp computes indices.
  //
  // Forall and Exists are Bool nodes whose `symbol` holds the number n of
  // variables they bind. Their children are n declaration leaves
  // BoundVariable(n-1-i, sort_i) in SMT-LIB declaration order, then the body,
  // then zero or more Pattern nodes. The declarations are what keep an unused
  // variable's sort in the hash key.
  //
  // Pattern is a Bool node holding the terms of one :pattern attribute.
  BoundVariable,
  Forall,
  Exists,
  Pattern,
};

struct Term {
  TermId id = 0;
  TermKind kind = TermKind::BoolValue;
  SortId sort = 0;
  SymbolId symbol = kInvalidSymbolId;
  std::vector<TermId> children;
  bool bool_value = false;
  util::Rational rational_value;
};

}  // namespace primo::core
