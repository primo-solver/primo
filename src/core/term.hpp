#pragma once

#include <vector>

#include "core/ids.hpp"

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
};

struct Term {
  TermId id = 0;
  TermKind kind = TermKind::BoolValue;
  SortId sort = 0;
  SymbolId symbol = kInvalidSymbolId;
  std::vector<TermId> children;
  bool bool_value = false;
};

}  // namespace primo::core
