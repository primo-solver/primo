#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"

#include <cassert>
#include <stdexcept>
#include <vector>

using primo::core::SortKind;
using primo::core::SymbolKind;
using primo::core::SymbolTable;
using primo::core::TermKind;
using primo::core::TermManager;

namespace {

template <typename Fn>
void expect_invalid_argument(Fn fn) {
  try {
    fn();
  } catch (const std::invalid_argument&) {
    return;
  }
  assert(false && "expected std::invalid_argument");
}

void symbol_table_tracks_sorts_and_symbols() {
  SymbolTable symbols;
  const auto bool_sort = symbols.bool_sort();
  assert(symbols.get_sort(bool_sort).name == "Bool");
  assert(symbols.get_sort(bool_sort).kind == SortKind::Bool);

  const auto sort_a = symbols.declare_sort("A");
  assert(symbols.lookup_sort("A") == sort_a);
  assert(symbols.get_sort(sort_a).kind == SortKind::Uninterpreted);

  const auto a = symbols.declare_constant("a", sort_a);
  const auto f = symbols.declare_function("f", {sort_a}, sort_a);
  const auto p = symbols.declare_predicate("p", {sort_a});

  assert(symbols.lookup_symbol("a") == a);
  assert(symbols.get_symbol(a).kind == SymbolKind::Constant);
  assert(symbols.get_symbol(f).arg_sorts == std::vector{sort_a});
  assert(symbols.get_symbol(p).result_sort == bool_sort);

  expect_invalid_argument([&] { symbols.declare_sort("A"); });
  expect_invalid_argument([&] { symbols.declare_constant("a", sort_a); });
}

void term_manager_hash_conses_and_checks_sorts() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto sort_b = symbols.declare_sort("B");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto b_symbol = symbols.declare_constant("b", sort_b);
  const auto f_symbol = symbols.declare_function("f", {sort_a}, sort_a);
  const auto pred_symbol = symbols.declare_predicate("p", {sort_a});

  TermManager terms(symbols);
  const auto a0 = terms.mk_constant(a_symbol);
  const auto a1 = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  assert(a0 == a1);
  assert(terms.get(a0).sort == sort_a);
  assert(terms.size() == 2);

  const auto f_a0 = terms.mk_app(f_symbol, {a0});
  const auto f_a1 = terms.mk_app(f_symbol, {a1});
  assert(f_a0 == f_a1);
  assert(terms.get(f_a0).kind == TermKind::Application);

  const auto p_a = terms.mk_app(pred_symbol, {a0});
  assert(terms.get(p_a).sort == symbols.bool_sort());

  const auto eq0 = terms.mk_eq(a0, f_a0);
  const auto eq1 = terms.mk_eq(a1, f_a1);
  assert(eq0 == eq1);
  assert(terms.get(eq0).kind == TermKind::Equality);
  assert(terms.get(eq0).sort == symbols.bool_sort());

  const auto negated_eq = terms.mk_not(eq0);
  assert(terms.get(negated_eq).children == std::vector{eq0});

  const auto conjunction = terms.mk_and({eq0, negated_eq, p_a});
  assert(terms.get(conjunction).children.size() == 3);

  expect_invalid_argument([&] { terms.mk_eq(a0, b); });
  expect_invalid_argument([&] { terms.mk_app(f_symbol, {b}); });
  expect_invalid_argument([&] { terms.mk_not(a0); });
  expect_invalid_argument([&] { terms.mk_app(a_symbol, {}); });
}

void Boolean_terms_are_hash_consed() {
  SymbolTable symbols;
  TermManager terms(symbols);

  const auto t0 = terms.mk_bool(true);
  const auto t1 = terms.mk_bool(true);
  const auto f0 = terms.mk_bool(false);
  assert(t0 == t1);
  assert(t0 != f0);
  assert(terms.get(t0).bool_value);
  assert(!terms.get(f0).bool_value);
}

}  // namespace

int main() {
  symbol_table_tracks_sorts_and_symbols();
  term_manager_hash_conses_and_checks_sorts();
  Boolean_terms_are_hash_consed();
  return 0;
}
