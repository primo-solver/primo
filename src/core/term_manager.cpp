#include "core/term_manager.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace primo::core {

namespace {

void hash_combine(std::size_t& seed, std::size_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

bool append_unique(std::vector<TermId>& result, std::unordered_set<TermId>* large_seen,
                   TermId child) {
  if (large_seen != nullptr) {
    if (!large_seen->insert(child).second) {
      return false;
    }
  } else if (std::find(result.begin(), result.end(), child) != result.end()) {
    return false;
  }
  result.push_back(child);
  return true;
}

}  // namespace

TermManager::TermManager(const SymbolTable& symbols) : symbols_(symbols) {}

TermId TermManager::mk_bool(bool value) {
  return intern(Key{
      TermKind::BoolValue, symbols_.bool_sort(), kInvalidSymbolId, {}, value, util::Rational(0)});
}

TermId TermManager::mk_constant(SymbolId symbol_id) {
  const Symbol& symbol = symbols_.get_symbol(symbol_id);
  if (symbol.kind != SymbolKind::Constant || !symbol.arg_sorts.empty()) {
    throw std::invalid_argument("symbol is not a constant");
  }
  return intern(
      Key{TermKind::Constant, symbol.result_sort, symbol_id, {}, false, util::Rational(0)});
}

TermId TermManager::mk_app(SymbolId symbol_id, std::vector<TermId> args) {
  const Symbol& symbol = symbols_.get_symbol(symbol_id);
  if (symbol.kind == SymbolKind::Constant) {
    throw std::invalid_argument("constant symbols must be created with mk_constant");
  }
  if (args.size() != symbol.arg_sorts.size()) {
    throw std::invalid_argument("application arity mismatch");
  }
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (require_term(args[i]).sort != symbol.arg_sorts[i]) {
      throw std::invalid_argument("application argument sort mismatch");
    }
  }
  return intern(Key{TermKind::Application, symbol.result_sort, symbol_id, std::move(args), false,
                    util::Rational(0)});
}

TermId TermManager::mk_eq(TermId lhs, TermId rhs) {
  const Term& lhs_term = require_term(lhs);
  const Term& rhs_term = require_term(rhs);
  if (lhs_term.sort != rhs_term.sort) {
    throw std::invalid_argument("equality sort mismatch");
  }
  if (lhs == rhs) {
    return mk_bool(true);
  }
  return intern(Key{TermKind::Equality,
                    symbols_.bool_sort(),
                    kInvalidSymbolId,
                    {lhs, rhs},
                    false,
                    util::Rational(0)});
}

TermId TermManager::mk_not(TermId child) {
  const Term& child_term = require_term(child);
  if (child_term.sort != symbols_.bool_sort()) {
    throw std::invalid_argument("expected Boolean term");
  }
  if (child_term.kind == TermKind::BoolValue) {
    return mk_bool(!child_term.bool_value);
  }
  if (child_term.kind == TermKind::Not) {
    return child_term.children.at(0);
  }
  return intern(Key{
      TermKind::Not, symbols_.bool_sort(), kInvalidSymbolId, {child}, false, util::Rational(0)});
}

TermId TermManager::mk_and(std::vector<TermId> children) {
  std::vector<TermId> simplified;
  simplified.reserve(children.size());
  std::unordered_set<TermId> seen;
  std::unordered_set<TermId>* large_seen = nullptr;
  if (children.size() > 8) {
    seen.reserve(children.size());
    large_seen = &seen;
  }
  for (const TermId child : children) {
    const Term& child_term = require_term(child);
    if (child_term.sort != symbols_.bool_sort()) {
      throw std::invalid_argument("expected Boolean term");
    }
    if (child_term.kind == TermKind::BoolValue) {
      if (!child_term.bool_value) {
        return mk_bool(false);
      }
      continue;
    }
    (void)append_unique(simplified, large_seen, child);
  }
  if (simplified.empty()) {
    return mk_bool(true);
  }
  if (simplified.size() == 1) {
    return simplified.front();
  }
  return intern(Key{TermKind::And, symbols_.bool_sort(), kInvalidSymbolId, std::move(simplified),
                    false, util::Rational(0)});
}

TermId TermManager::mk_or(std::vector<TermId> children) {
  std::vector<TermId> simplified;
  simplified.reserve(children.size());
  std::unordered_set<TermId> seen;
  std::unordered_set<TermId>* large_seen = nullptr;
  if (children.size() > 8) {
    seen.reserve(children.size());
    large_seen = &seen;
  }
  for (const TermId child : children) {
    const Term& child_term = require_term(child);
    if (child_term.sort != symbols_.bool_sort()) {
      throw std::invalid_argument("expected Boolean term");
    }
    if (child_term.kind == TermKind::BoolValue) {
      if (child_term.bool_value) {
        return mk_bool(true);
      }
      continue;
    }
    (void)append_unique(simplified, large_seen, child);
  }
  if (simplified.empty()) {
    return mk_bool(false);
  }
  if (simplified.size() == 1) {
    return simplified.front();
  }
  return intern(Key{TermKind::Or, symbols_.bool_sort(), kInvalidSymbolId, std::move(simplified),
                    false, util::Rational(0)});
}

TermId TermManager::mk_implies(TermId lhs, TermId rhs) {
  const Term& lhs_term = require_term(lhs);
  const Term& rhs_term = require_term(rhs);
  if (lhs_term.sort != symbols_.bool_sort() || rhs_term.sort != symbols_.bool_sort()) {
    throw std::invalid_argument("expected Boolean term");
  }
  if (lhs == rhs) {
    return mk_bool(true);
  }
  if (lhs_term.kind == TermKind::BoolValue) {
    return lhs_term.bool_value ? rhs : mk_bool(true);
  }
  if (rhs_term.kind == TermKind::BoolValue) {
    return rhs_term.bool_value ? mk_bool(true) : mk_not(lhs);
  }
  return intern(Key{TermKind::Implies,
                    symbols_.bool_sort(),
                    kInvalidSymbolId,
                    {lhs, rhs},
                    false,
                    util::Rational(0)});
}

TermId TermManager::mk_rational(util::Rational value) {
  return mk_rational(std::move(value), symbols_.real_sort());
}

TermId TermManager::mk_rational(util::Rational value, SortId sort) {
  if (!is_arithmetic_sort(sort)) {
    throw std::invalid_argument("expected Real or Int sort");
  }
  if (sort == symbols_.int_sort() && !value.is_integer()) {
    throw std::invalid_argument("Int literal must be an integer value");
  }
  return intern(Key{TermKind::Rational, sort, kInvalidSymbolId, {}, false, std::move(value)});
}

TermId TermManager::mk_plus(std::vector<TermId> children) {
  if (children.empty()) {
    throw std::invalid_argument("+ requires at least one argument");
  }

  const SortId sort = require_term(children.front()).sort;
  if (!is_arithmetic_sort(sort)) {
    throw std::invalid_argument("expected Real or Int term");
  }

  std::vector<TermId> flattened;
  flattened.reserve(children.size());
  util::Rational constant(0);
  for (const TermId child : children) {
    const Term& child_term = require_term(child);
    if (child_term.sort != sort) {
      throw std::invalid_argument("arithmetic terms must share one sort");
    }
    if (child_term.kind == TermKind::Rational) {
      constant += child_term.rational_value;
      continue;
    }
    if (child_term.kind == TermKind::Plus) {
      flattened.insert(flattened.end(), child_term.children.begin(), child_term.children.end());
      continue;
    }
    flattened.push_back(child);
  }

  if (flattened.empty()) {
    return mk_rational(constant, sort);
  }
  if (!constant.is_zero()) {
    flattened.push_back(mk_rational(constant, sort));
  }
  if (flattened.size() == 1) {
    return flattened.front();
  }
  return intern(
      Key{TermKind::Plus, sort, kInvalidSymbolId, std::move(flattened), false, util::Rational(0)});
}

TermId TermManager::mk_times(util::Rational coefficient, TermId child) {
  const Term& child_term = require_term(child);
  const SortId sort = child_term.sort;
  if (!is_arithmetic_sort(sort)) {
    throw std::invalid_argument("expected Real or Int term");
  }
  if (sort == symbols_.int_sort() && !coefficient.is_integer()) {
    throw std::invalid_argument("Int coefficient must be an integer value");
  }
  if (coefficient.is_zero()) {
    return mk_rational(util::Rational(0), sort);
  }
  if (child_term.kind == TermKind::Rational) {
    return mk_rational(coefficient * child_term.rational_value, sort);
  }
  if (child_term.kind == TermKind::Times) {
    return mk_times(coefficient * child_term.rational_value, child_term.children.at(0));
  }
  if (coefficient == util::Rational(1)) {
    return child;
  }
  return intern(
      Key{TermKind::Times, sort, kInvalidSymbolId, {child}, false, std::move(coefficient)});
}

TermId TermManager::mk_le(TermId lhs, TermId rhs) {
  const Term& lhs_term = require_term(lhs);
  const Term& rhs_term = require_term(rhs);
  if (lhs_term.sort != rhs_term.sort || !is_arithmetic_sort(lhs_term.sort)) {
    throw std::invalid_argument("expected two terms of the same Real or Int sort");
  }
  if (lhs == rhs) {
    return mk_bool(true);
  }
  if (lhs_term.kind == TermKind::Rational && rhs_term.kind == TermKind::Rational) {
    return mk_bool(lhs_term.rational_value <= rhs_term.rational_value);
  }
  return intern(Key{
      TermKind::Le, symbols_.bool_sort(), kInvalidSymbolId, {lhs, rhs}, false, util::Rational(0)});
}

TermId TermManager::mk_lt(TermId lhs, TermId rhs) {
  const Term& lhs_term = require_term(lhs);
  const Term& rhs_term = require_term(rhs);
  if (lhs_term.sort != rhs_term.sort || !is_arithmetic_sort(lhs_term.sort)) {
    throw std::invalid_argument("expected two terms of the same Real or Int sort");
  }
  if (lhs == rhs) {
    return mk_bool(false);
  }
  if (lhs_term.kind == TermKind::Rational && rhs_term.kind == TermKind::Rational) {
    return mk_bool(lhs_term.rational_value < rhs_term.rational_value);
  }
  return intern(Key{
      TermKind::Lt, symbols_.bool_sort(), kInvalidSymbolId, {lhs, rhs}, false, util::Rational(0)});
}

// Interning is idempotent, so the extended-term counter must follow the arena
// rather than the call: a hash-cons hit creates no node and must not count.
TermId TermManager::intern_extended(Key key) {
  const std::size_t before = terms_.size();
  const TermId id = intern(std::move(key));
  if (terms_.size() != before) {
    ++extended_term_count_;
  }
  return id;
}

TermId TermManager::mk_ite(TermId condition, TermId then_term, TermId else_term) {
  const Term& condition_term = require_term(condition);
  const Term& then_branch = require_term(then_term);
  if (condition_term.sort != symbols_.bool_sort()) {
    throw std::invalid_argument("ite condition is not Boolean");
  }
  if (then_branch.sort != require_term(else_term).sort) {
    throw std::invalid_argument("ite branch sort mismatch");
  }
  // Constant folding only. Rewriting an ite -- collapsing equal branches,
  // normalizing a negated condition, merging a nested ite on the same
  // condition, turning Boolean branches into connectives -- is reasoning
  // about the term rather than defining it, and it belongs to
  // simplify-extended-terms where it can be measured and switched off. That
  // matters here specifically: those rules are the trivial end of the
  // if-then-else rewrite system in lead 16, whose own falsifier requires the
  // techniques to be evaluated together rather than shipped piecemeal, and a
  // constructor rewrite cannot be A/B'd.
  if (condition_term.kind == TermKind::BoolValue) {
    return condition_term.bool_value ? then_term : else_term;
  }
  return intern_extended(Key{TermKind::Ite,
                             then_branch.sort,
                             kInvalidSymbolId,
                             {condition, then_term, else_term},
                             false,
                             util::Rational(0)});
}

namespace {

// Euclidean quotient and remainder for a positive divisor, where Boute's
// definition coincides with floored division.
std::pair<util::Rational, util::Rational> euclidean_divide(const util::Rational& numerator,
                                                           const util::Rational& divisor) {
  const util::Rational quotient = (numerator / divisor).floor();
  return {quotient, numerator - divisor * quotient};
}

}  // namespace

// Shared checking and sign normalization for div and mod. Returns the
// positive divisor to store; the caller applies the sign identity.
util::Rational TermManager::normalized_int_divisor(const util::Rational& divisor,
                                                   TermId numerator) const {
  if (require_term(numerator).sort != symbols_.int_sort()) {
    throw std::invalid_argument("div and mod expect an Int numerator");
  }
  if (!divisor.is_integer()) {
    throw std::invalid_argument("div and mod require an integer literal divisor");
  }
  if (divisor.is_zero()) {
    throw std::invalid_argument("division by zero is not supported");
  }
  return divisor < util::Rational(0) ? -divisor : divisor;
}

TermId TermManager::mk_int_div(util::Rational divisor, TermId numerator) {
  const util::Rational positive = normalized_int_divisor(divisor, numerator);
  // divE(D, -d) = -divE(D, d): recover the sign the caller asked for.
  if (divisor < util::Rational(0)) {
    return mk_times(util::Rational(-1), mk_int_div(positive, numerator));
  }
  const Term& numerator_term = require_term(numerator);
  if (numerator_term.kind == TermKind::Rational) {
    return mk_rational(euclidean_divide(numerator_term.rational_value, positive).first,
                       symbols_.int_sort());
  }
  return intern_extended(
      Key{TermKind::IntDiv, symbols_.int_sort(), kInvalidSymbolId, {numerator}, false, positive});
}

TermId TermManager::mk_int_mod(util::Rational divisor, TermId numerator) {
  const util::Rational positive = normalized_int_divisor(divisor, numerator);
  // modE(D, -d) = modE(D, d): the sign of the divisor does not reach the
  // remainder at all, which is the point of the Euclidean definition.
  const Term& numerator_term = require_term(numerator);
  if (numerator_term.kind == TermKind::Rational) {
    return mk_rational(euclidean_divide(numerator_term.rational_value, positive).second,
                       symbols_.int_sort());
  }
  return intern_extended(
      Key{TermKind::IntMod, symbols_.int_sort(), kInvalidSymbolId, {numerator}, false, positive});
}

TermId TermManager::mk_abs(TermId operand) {
  const Term& operand_term = require_term(operand);
  if (operand_term.sort != symbols_.int_sort()) {
    throw std::invalid_argument("abs expects an Int term");
  }
  if (operand_term.kind == TermKind::Rational) {
    const util::Rational& value = operand_term.rational_value;
    return mk_rational(value < util::Rational(0) ? -value : value, symbols_.int_sort());
  }
  return intern_extended(Key{
      TermKind::Abs, symbols_.int_sort(), kInvalidSymbolId, {operand}, false, util::Rational(0)});
}

std::size_t TermManager::extended_term_count() const { return extended_term_count_; }

const Term& TermManager::get(TermId id) const { return require_term(id); }

const SymbolTable& TermManager::symbols() const { return symbols_; }

std::size_t TermManager::size() const { return terms_.size(); }

const std::vector<Term>& TermManager::terms() const { return terms_; }

std::size_t TermManager::KeyHash::operator()(const Key& key) const {
  std::size_t seed = 0;
  hash_combine(seed, static_cast<std::size_t>(key.kind));
  hash_combine(seed, key.sort);
  hash_combine(seed, key.symbol);
  hash_combine(seed, key.bool_value ? 1U : 0U);
  hash_combine(seed, util::RationalHash{}(key.rational_value));
  for (const TermId child : key.children) {
    hash_combine(seed, child);
  }
  return seed;
}

TermId TermManager::intern(Key key) {
  const auto existing = term_by_key_.find(key);
  if (existing != term_by_key_.end()) {
    return existing->second;
  }

  const TermId id = static_cast<TermId>(terms_.size());
  Term term;
  term.id = id;
  term.kind = key.kind;
  term.sort = key.sort;
  term.symbol = key.symbol;
  term.children = key.children;
  term.bool_value = key.bool_value;
  term.rational_value = key.rational_value;
  terms_.push_back(std::move(term));
  term_by_key_.emplace(std::move(key), id);
  return id;
}

const Term& TermManager::require_term(TermId id) const {
  if (id >= terms_.size()) {
    throw std::out_of_range("unknown term id");
  }
  return terms_[id];
}

void TermManager::require_bool(TermId id) const {
  if (require_term(id).sort != symbols_.bool_sort()) {
    throw std::invalid_argument("expected Boolean term");
  }
}

bool TermManager::is_arithmetic_sort(SortId sort) const {
  return sort == symbols_.real_sort() || sort == symbols_.int_sort();
}

}  // namespace primo::core
