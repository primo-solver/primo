#include "core/symbol_table.hpp"

#include <stdexcept>
#include <utility>

namespace primo::core {

SymbolTable::SymbolTable() {
  add_sort("Bool", SortKind::Bool);
  add_sort("Real", SortKind::Real);
  add_sort("Int", SortKind::Int);
}

SortId SymbolTable::bool_sort() const { return 0; }

SortId SymbolTable::real_sort() const { return 1; }

SortId SymbolTable::int_sort() const { return 2; }

SortId SymbolTable::declare_sort(std::string name) {
  return add_sort(std::move(name), SortKind::Uninterpreted);
}

std::optional<SortId> SymbolTable::lookup_sort(const std::string& name) const {
  const auto it = sort_by_name_.find(name);
  if (it == sort_by_name_.end()) {
    return std::nullopt;
  }
  return it->second;
}

const Sort& SymbolTable::get_sort(SortId id) const {
  require_sort(id);
  return sorts_[id];
}

SymbolId SymbolTable::declare_constant(std::string name, SortId sort) {
  return add_symbol(std::move(name), {}, sort, SymbolKind::Constant);
}

SymbolId SymbolTable::declare_function(std::string name, std::vector<SortId> arg_sorts,
                                       SortId result_sort) {
  return add_symbol(std::move(name), std::move(arg_sorts), result_sort, SymbolKind::Function);
}

SymbolId SymbolTable::declare_predicate(std::string name, std::vector<SortId> arg_sorts) {
  return add_symbol(std::move(name), std::move(arg_sorts), bool_sort(), SymbolKind::Predicate);
}

std::optional<SymbolId> SymbolTable::lookup_symbol(const std::string& name) const {
  const auto it = symbol_by_name_.find(name);
  if (it == symbol_by_name_.end()) {
    return std::nullopt;
  }
  return it->second;
}

const Symbol& SymbolTable::get_symbol(SymbolId id) const {
  if (id >= symbols_.size()) {
    throw std::out_of_range("unknown symbol id");
  }
  return symbols_[id];
}

const std::vector<Sort>& SymbolTable::sorts() const { return sorts_; }

const std::vector<Symbol>& SymbolTable::symbols() const { return symbols_; }

SortId SymbolTable::add_sort(std::string name, SortKind kind) {
  if (name.empty()) {
    throw std::invalid_argument("sort name must not be empty");
  }
  if (sort_by_name_.contains(name)) {
    throw std::invalid_argument("duplicate sort declaration: " + name);
  }
  const SortId id = static_cast<SortId>(sorts_.size());
  sorts_.push_back(Sort{id, name, kind});
  sort_by_name_.emplace(std::move(name), id);
  return id;
}

SymbolId SymbolTable::add_symbol(std::string name, std::vector<SortId> arg_sorts,
                                 SortId result_sort, SymbolKind kind) {
  if (name.empty()) {
    throw std::invalid_argument("symbol name must not be empty");
  }
  if (symbol_by_name_.contains(name)) {
    throw std::invalid_argument("duplicate symbol declaration: " + name);
  }
  for (const SortId sort : arg_sorts) {
    require_sort(sort);
  }
  require_sort(result_sort);

  const SymbolId id = static_cast<SymbolId>(symbols_.size());
  symbols_.push_back(Symbol{id, name, std::move(arg_sorts), result_sort, kind});
  symbol_by_name_.emplace(std::move(name), id);
  return id;
}

void SymbolTable::require_sort(SortId id) const {
  if (id >= sorts_.size()) {
    throw std::out_of_range("unknown sort id");
  }
}

}  // namespace primo::core
