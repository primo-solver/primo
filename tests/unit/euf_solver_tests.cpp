#include <cassert>

#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "theory/equality/euf_solver.hpp"

using primo::theory::equality::EufSolver;

namespace {

void congruent_applications_are_merged() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto f = symbols.declare_function("f", {sort}, sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto f_a = terms.mk_app(f, {a_term});
  const auto f_b = terms.mk_app(f, {b_term});

  EufSolver solver(terms);
  solver.assert_equal(a_term, b_term);
  solver.assert_distinct(f_a, f_b);
  assert(!solver.is_satisfiable());
}

void distinct_unrelated_terms_are_satisfiable() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);

  EufSolver solver(terms);
  solver.assert_distinct(a_term, b_term);
  assert(solver.is_satisfiable());
}

}  // namespace

int main() {
  congruent_applications_are_merged();
  distinct_unrelated_terms_are_satisfiable();
  return 0;
}
