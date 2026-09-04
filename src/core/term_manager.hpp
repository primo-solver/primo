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

  // Linear arithmetic (QF_LRA and QF_LIA share these -- a term's `sort`
  // field, Real or Int, is what tells them apart; nothing else about the
  // representation differs). See the TermKind comment in term.hpp for why
  // mk_times takes a single child (coefficient * child, not general
  // multiplication) and there is no mk_minus/mk_ge/mk_gt. `sort` must be
  // real_sort() or int_sort(); for int_sort() the value must already be an
  // integer (SortKind::Int has no non-integer literals). The one-argument
  // overload is real_sort() shorthand, for the many QF_LRA call sites that
  // predate QF_LIA and only ever meant Real.
  TermId mk_rational(util::Rational value);
  TermId mk_rational(util::Rational value, SortId sort);
  TermId mk_plus(std::vector<TermId> children);
  TermId mk_times(util::Rational coefficient, TermId child);
  TermId mk_le(TermId lhs, TermId rhs);
  TermId mk_lt(TermId lhs, TermId rhs);

  // Structure preprocessing is allowed to see. These constructors fold only
  // what is unconditional -- literal arguments, and the two ite identities
  // that would otherwise force a pointless auxiliary. Everything else is left
  // for the preprocessing leaf so that it is measurable. mk_int_div and
  // mk_int_mod additionally normalize a negative divisor away, which is a
  // representation invariant; see the TermKind comment.
  TermId mk_ite(TermId condition, TermId then_term, TermId else_term);
  TermId mk_int_div(util::Rational divisor, TermId numerator);
  TermId mk_int_mod(util::Rational divisor, TermId numerator);
  TermId mk_abs(TermId operand);

  // Number of Ite/IntDiv/IntMod/Abs nodes ever interned. Zero lets the
  // lowering leaf skip its traversal outright, which is the common case:
  // roughly seven in ten benchmarks contain no ite at all.
  std::size_t extended_term_count() const;

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
  TermId intern_extended(Key key);
  util::Rational normalized_int_divisor(const util::Rational& divisor, TermId numerator) const;
  const Term& require_term(TermId id) const;
  void require_bool(TermId id) const;
  bool is_arithmetic_sort(SortId sort) const;

  const SymbolTable& symbols_;
  std::vector<Term> terms_;
  std::size_t extended_term_count_ = 0;
  std::unordered_map<Key, TermId, KeyHash> term_by_key_;
};

}  // namespace primo::core
