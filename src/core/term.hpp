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
