#include <cassert>

#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "sat/lit.hpp"
#include "theory/equality/euf_solver.hpp"
#include "theory/equality/explained_congruence_closure.hpp"

using primo::theory::equality::EufSolver;
using primo::theory::equality::ExplainedCongruenceClosure;

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

void explained_closure_explains_explicit_chain() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto c = symbols.declare_constant("c", sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto c_term = terms.mk_constant(c);

  ExplainedCongruenceClosure closure(terms);
  closure.assert_equal(a_term, b_term, {primo::sat::Lit{-1}});
  closure.assert_equal(b_term, c_term, {primo::sat::Lit{-2}});
  const auto reason = closure.explain_equal(a_term, c_term);

  assert(reason.has_value());
  assert(reason->size() == 2);
}

void explained_closure_explains_congruence() {
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

  ExplainedCongruenceClosure closure(terms);
  closure.assert_equal(a_term, b_term, {primo::sat::Lit{-1}});
  const auto reason = closure.explain_equal(f_a, f_b);

  assert(reason.has_value());
  assert(reason->size() == 1);
  assert(reason->front().value == -1);
}

}  // namespace

int main() {
  congruent_applications_are_merged();
  distinct_unrelated_terms_are_satisfiable();
  explained_closure_explains_explicit_chain();
  explained_closure_explains_congruence();
  return 0;
}
