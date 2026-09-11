#pragma once

#include <cstddef>
#include <cstdint>
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

  // Quantifiers with de Bruijn bound variables; see the TermKind comment and
  // docs/quantifiers.md. mk_forall and mk_exists take the variable sorts in
  // SMT-LIB declaration order, so the last variable has index 0 in the body.
  // Patterns must be Pattern nodes, whose terms must not contain quantifiers.
  // These constructors never rename or shift; core/binders.hpp does.
  TermId mk_bound_variable(std::uint32_t index, SortId sort);
  TermId mk_forall(const std::vector<SortId>& sorts, TermId body,
                   std::vector<TermId> patterns = {});
  TermId mk_exists(const std::vector<SortId>& sorts, TermId body,
                   std::vector<TermId> patterns = {});
  TermId mk_pattern(std::vector<TermId> terms);

  // One past the largest bound-variable index that is not bound inside the
  // term, so 0 exactly for closed terms. Cached per node at intern time. The
  // storage is created by the first binder node, which is why quantifier-free
  // sessions pay nothing and every earlier term correctly reads as closed.
  std::uint32_t loose_index(TermId id) const;
  bool contains_quantifier(TermId id) const;
  // Number of Forall/Exists nodes ever interned.
  std::size_t quantifier_count() const;

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
  TermId mk_quantifier(TermKind kind, const std::vector<SortId>& sorts, TermId body,
                       std::vector<TermId> patterns);
  void record_binder_facts(TermId id);
  util::Rational normalized_int_divisor(const util::Rational& divisor, TermId numerator) const;
  const Term& require_term(TermId id) const;
  void require_bool(TermId id) const;
  bool is_arithmetic_sort(SortId sort) const;

  const SymbolTable& symbols_;
  std::vector<Term> terms_;
  std::size_t extended_term_count_ = 0;
  std::size_t quantifier_count_ = 0;
  bool binder_facts_active_ = false;
  std::vector<std::uint32_t> loose_index_;
  std::vector<bool> contains_quantifier_;
  std::unordered_map<Key, TermId, KeyHash> term_by_key_;
};

}  // namespace primo::core
