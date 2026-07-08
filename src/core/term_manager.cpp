#include "core/term_manager.hpp"

#include <stdexcept>
#include <utility>

namespace primo::core {

namespace {

void hash_combine(std::size_t& seed, std::size_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

}  // namespace

TermManager::TermManager(const SymbolTable& symbols) : symbols_(symbols) {}

TermId TermManager::mk_bool(bool value) {
  return intern(Key{TermKind::BoolValue, symbols_.bool_sort(), kInvalidSymbolId, {}, value});
}

TermId TermManager::mk_constant(SymbolId symbol_id) {
  const Symbol& symbol = symbols_.get_symbol(symbol_id);
  if (symbol.kind != SymbolKind::Constant || !symbol.arg_sorts.empty()) {
    throw std::invalid_argument("symbol is not a constant");
  }
  return intern(Key{TermKind::Constant, symbol.result_sort, symbol_id, {}, false});
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
  return intern(Key{TermKind::Application, symbol.result_sort, symbol_id, std::move(args), false});
}

TermId TermManager::mk_eq(TermId lhs, TermId rhs) {
  const Term& lhs_term = require_term(lhs);
  const Term& rhs_term = require_term(rhs);
  if (lhs_term.sort != rhs_term.sort) {
    throw std::invalid_argument("equality sort mismatch");
  }
  return intern(Key{TermKind::Equality, symbols_.bool_sort(), kInvalidSymbolId, {lhs, rhs}, false});
}

TermId TermManager::mk_not(TermId child) {
  require_bool(child);
  return intern(Key{TermKind::Not, symbols_.bool_sort(), kInvalidSymbolId, {child}, false});
}

TermId TermManager::mk_and(std::vector<TermId> children) {
  for (const TermId child : children) {
    require_bool(child);
  }
  return intern(
      Key{TermKind::And, symbols_.bool_sort(), kInvalidSymbolId, std::move(children), false});
}

TermId TermManager::mk_or(std::vector<TermId> children) {
  for (const TermId child : children) {
    require_bool(child);
  }
  return intern(
      Key{TermKind::Or, symbols_.bool_sort(), kInvalidSymbolId, std::move(children), false});
}

TermId TermManager::mk_implies(TermId lhs, TermId rhs) {
  require_bool(lhs);
  require_bool(rhs);
  return intern(Key{TermKind::Implies, symbols_.bool_sort(), kInvalidSymbolId, {lhs, rhs}, false});
}

const Term& TermManager::get(TermId id) const { return require_term(id); }

std::size_t TermManager::size() const { return terms_.size(); }

std::size_t TermManager::KeyHash::operator()(const Key& key) const {
  std::size_t seed = 0;
  hash_combine(seed, static_cast<std::size_t>(key.kind));
  hash_combine(seed, key.sort);
  hash_combine(seed, key.symbol);
  hash_combine(seed, key.bool_value ? 1U : 0U);
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

}  // namespace primo::core
