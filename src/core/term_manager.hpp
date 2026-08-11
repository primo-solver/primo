#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "util/rational.hpp"

namespace primo::core {

class TermManager {
 public:
  explicit TermManager(const SymbolTable& symbols);

  TermId mk_bool(bool value);
  TermId mk_constant(SymbolId symbol);
  TermId mk_app(SymbolId symbol, std::vector<TermId> args);
  TermId mk_eq(TermId lhs, TermId rhs);
  TermId mk_not(TermId child);
  // Preserve nested associative nodes so hash-consed callers retain DAG
  // sharing. Wider And/Or nodes are an explicit SMT preprocessing decision.
  TermId mk_and(std::vector<TermId> children);
  TermId mk_or(std::vector<TermId> children);
  TermId mk_implies(TermId lhs, TermId rhs);

  // Linear real arithmetic (QF_LRA). See the TermKind comment in term.hpp
  // for why mk_times takes a single child (coefficient * child, not
  // general multiplication) and there is no mk_minus/mk_ge/mk_gt.
  TermId mk_rational(util::Rational value);
  TermId mk_plus(std::vector<TermId> children);
  TermId mk_times(util::Rational coefficient, TermId child);
  TermId mk_le(TermId lhs, TermId rhs);
  TermId mk_lt(TermId lhs, TermId rhs);

  const Term& get(TermId id) const;
  const SymbolTable& symbols() const;
  std::size_t size() const;
  const std::vector<Term>& terms() const;

 private:
  struct Key {
    TermKind kind = TermKind::BoolValue;
    SortId sort = kInvalidSortId;
    SymbolId symbol = kInvalidSymbolId;
    std::vector<TermId> children;
    bool bool_value = false;
    util::Rational rational_value;

    bool operator==(const Key& other) const = default;
  };

  struct KeyHash {
    std::size_t operator()(const Key& key) const;
  };

  TermId intern(Key key);
  const Term& require_term(TermId id) const;
  void require_bool(TermId id) const;

  const SymbolTable& symbols_;
  std::vector<Term> terms_;
  std::unordered_map<Key, TermId, KeyHash> term_by_key_;
};

}  // namespace primo::core
