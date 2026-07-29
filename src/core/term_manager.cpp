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
  if (lhs == rhs) {
    return mk_bool(true);
  }
  return intern(Key{TermKind::Equality, symbols_.bool_sort(), kInvalidSymbolId, {lhs, rhs}, false});
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
  return intern(Key{TermKind::Not, symbols_.bool_sort(), kInvalidSymbolId, {child}, false});
}

TermId TermManager::mk_and(std::vector<TermId> children) {
  std::vector<TermId> simplified;
  simplified.reserve(children.size());
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
    if (child_term.kind == TermKind::And) {
      simplified.insert(simplified.end(), child_term.children.begin(), child_term.children.end());
      continue;
    }
    simplified.push_back(child);
  }
  if (simplified.empty()) {
    return mk_bool(true);
  }
  if (simplified.size() == 1) {
    return simplified.front();
  }
  return intern(
      Key{TermKind::And, symbols_.bool_sort(), kInvalidSymbolId, std::move(simplified), false});
}

TermId TermManager::mk_or(std::vector<TermId> children) {
  std::vector<TermId> simplified;
  simplified.reserve(children.size());
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
    if (child_term.kind == TermKind::Or) {
      simplified.insert(simplified.end(), child_term.children.begin(), child_term.children.end());
      continue;
    }
    simplified.push_back(child);
  }
  if (simplified.empty()) {
    return mk_bool(false);
  }
  if (simplified.size() == 1) {
    return simplified.front();
  }
  return intern(
      Key{TermKind::Or, symbols_.bool_sort(), kInvalidSymbolId, std::move(simplified), false});
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
  return intern(Key{TermKind::Implies, symbols_.bool_sort(), kInvalidSymbolId, {lhs, rhs}, false});
}

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
