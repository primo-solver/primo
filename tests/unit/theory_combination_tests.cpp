#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <vector>

#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "smt/theory_combination.hpp"

using primo::core::SymbolTable;
using primo::core::TermManager;
using primo::smt::analyze_nelson_oppen_interface;

namespace {

void finds_visible_interface_constants_and_equalities_by_sort() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto sort_b = symbols.declare_sort("B");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto b_symbol = symbols.declare_constant("b", sort_a);
  const auto c_symbol = symbols.declare_constant("c", sort_a);
  const auto d_symbol = symbols.declare_constant("d", sort_b);
  const auto p_symbol = symbols.declare_predicate("p", {sort_a});
  const auto q_symbol = symbols.declare_predicate("q", {sort_b});

  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  const auto c = terms.mk_constant(c_symbol);
  const auto d = terms.mk_constant(d_symbol);
  const auto p_a = terms.mk_app(p_symbol, {a});
  const auto q_d = terms.mk_app(q_symbol, {d});
  const auto eq_ab = terms.mk_eq(a, b);
  const auto formula = terms.mk_and({eq_ab, terms.mk_not(terms.mk_eq(b, c)), p_a, q_d});

  const auto summary = analyze_nelson_oppen_interface(terms, {formula});
  assert(summary.terms.size() == 4);
  assert(summary.sort_count == 2);
  assert(summary.candidate_pairs == 3);
  assert(summary.visible_equalities.size() == 2);
  assert(summary.visible_equalities[0].lhs == a);
  assert(summary.visible_equalities[0].rhs == b);
  assert(summary.visible_equalities[1].lhs == b);
  assert(summary.visible_equalities[1].rhs == c);
}

void ignores_boolean_and_internal_constants() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto aux_symbol = symbols.declare_constant("$primo_aux_0", sort_a);
  const auto flag_symbol = symbols.declare_constant("flag", symbols.bool_sort());

  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto aux = terms.mk_constant(aux_symbol);
  const auto flag = terms.mk_constant(flag_symbol);
  const auto formula = terms.mk_and({terms.mk_eq(a, aux), flag});

  const auto summary = analyze_nelson_oppen_interface(terms, {formula});
  assert(summary.terms.size() == 1);
  assert(summary.terms[0].term == a);
  assert(summary.candidate_pairs == 0);
  assert(summary.visible_equalities.empty());
}

void deduplicates_reversed_visible_equality_atoms() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto b_symbol = symbols.declare_constant("b", sort_a);

  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);

  const auto summary =
      analyze_nelson_oppen_interface(terms, {terms.mk_eq(a, b), terms.mk_eq(b, a)});
  assert(summary.terms.size() == 2);
  assert(summary.candidate_pairs == 1);
  assert(summary.visible_equalities.size() == 1);
  assert(summary.visible_equalities[0].lhs == a);
  assert(summary.visible_equalities[0].rhs == b);
}

}  // namespace

int main() {
  finds_visible_interface_constants_and_equalities_by_sort();
  ignores_boolean_and_internal_constants();
  deduplicates_reversed_visible_equality_atoms();
  return 0;
}
