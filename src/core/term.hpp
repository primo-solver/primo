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
  // Linear real arithmetic (QF_LRA). `*` and `/` are resolved into a
  // literal Rational coefficient at parse time (see build_term / the
  // SMT-LIB QF_LRA `:extensions` grammar), so no Times/Minus/Ge/Gt kind is
  // needed: n-ary `+` folds into Plus, `-` desugars to Plus/Neg, and
  // `>=`/`>` desugar to flipped Le/Lt.
  Rational,
  Plus,
  Neg,
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
