#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/ids.hpp"
#include "core/sort.hpp"

namespace primo::core {

enum class SymbolKind {
  Constant,
  Function,
  Predicate,
};

struct Symbol {
  SymbolId id = kInvalidSymbolId;
  std::string name;
  std::vector<SortId> arg_sorts;
  SortId result_sort = kInvalidSortId;
  SymbolKind kind = SymbolKind::Constant;
};

class SymbolTable {
 public:
  SymbolTable();

  SortId bool_sort() const;
  SortId real_sort() const;
  SortId declare_sort(std::string name);
  std::optional<SortId> lookup_sort(const std::string& name) const;
  const Sort& get_sort(SortId id) const;

  SymbolId declare_constant(std::string name, SortId sort);
  SymbolId declare_function(std::string name, std::vector<SortId> arg_sorts, SortId result_sort);
  SymbolId declare_predicate(std::string name, std::vector<SortId> arg_sorts);
  std::optional<SymbolId> lookup_symbol(const std::string& name) const;
  const Symbol& get_symbol(SymbolId id) const;

 private:
  SortId add_sort(std::string name, SortKind kind);
  SymbolId add_symbol(std::string name, std::vector<SortId> arg_sorts, SortId result_sort,
                      SymbolKind kind);
  void require_sort(SortId id) const;

  std::vector<Sort> sorts_;
  std::vector<Symbol> symbols_;
  std::unordered_map<std::string, SortId> sort_by_name_;
  std::unordered_map<std::string, SymbolId> symbol_by_name_;
};

}  // namespace primo::core
