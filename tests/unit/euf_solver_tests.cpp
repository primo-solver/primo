#include <cstddef>
#include <cstdlib>

#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "sat/lit.hpp"
#include "theory/equality/euf_solver.hpp"
#include "theory/equality/explained_congruence_closure.hpp"
#include "theory/equality/flat_congruence_closure.hpp"
#include "theory/equality/flat_terms.hpp"
#include "theory/equality/incremental_congruence_closure.hpp"

using primo::theory::equality::EufSolver;
using primo::theory::equality::ExplainedCongruenceClosure;
using primo::theory::equality::FlatCongruenceClosure;
using primo::theory::equality::FlatTermIndex;
using primo::theory::equality::IncrementalCongruenceClosure;

namespace {

void check(bool condition) {
  if (!condition) {
    std::abort();
  }
}

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
  check(!solver.is_satisfiable());
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
  check(solver.is_satisfiable());
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

  check(reason.has_value());
  check(reason->size() == 2);
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

  check(reason.has_value());
  check(reason->size() == 1);
  check(reason->front().value == -1);
}

void incremental_closure_discovers_unary_congruence_with_lookup() {
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

  IncrementalCongruenceClosure closure(terms);
  closure.add_term(f_a);
  closure.add_term(f_b);
  closure.assert_equal(a_term, b_term, {primo::sat::Lit{-1}});

  check(closure.are_equal(f_a, f_b));
  check(closure.stats().lookup_hits >= 1);
  check(closure.stats().congruence_merges >= 1);
}

void incremental_closure_discovers_binary_shared_subterm_congruence() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto c = symbols.declare_constant("c", sort);
  const auto g = symbols.declare_function("g", {sort, sort}, sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto c_term = terms.mk_constant(c);
  const auto g_a_c = terms.mk_app(g, {a_term, c_term});
  const auto g_b_c = terms.mk_app(g, {b_term, c_term});

  IncrementalCongruenceClosure closure(terms);
  closure.add_term(g_a_c);
  closure.add_term(g_b_c);
  closure.assert_equal(a_term, b_term, {primo::sat::Lit{-2}});

  check(closure.are_equal(g_a_c, g_b_c));
  const auto reason = closure.explain_equal(g_a_c, g_b_c);
  check(reason.has_value());
  check(reason->size() == 1);
  check(reason->front().value == -2);
}

void incremental_closure_reports_disequality_conflict() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);

  IncrementalCongruenceClosure closure(terms);
  closure.assert_distinct(a_term, b_term, {primo::sat::Lit{-10}});
  check(!closure.conflict().has_value());
  closure.assert_equal(a_term, b_term, {primo::sat::Lit{-11}});

  const auto conflict = closure.conflict();
  check(conflict.has_value());
  check(conflict->size() == 2);
  check(closure.stats().disequality_checks >= 2);
  check(closure.stats().conflicts == 1);
}

void incremental_closure_backtracks_and_rediscoveres_congruence() {
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

  IncrementalCongruenceClosure closure(terms);
  closure.add_term(f_a);
  closure.add_term(f_b);
  const std::size_t checkpoint = closure.push();
  closure.assert_equal(a_term, b_term, {primo::sat::Lit{-3}});
  check(closure.are_equal(f_a, f_b));
  closure.pop(checkpoint);
  check(!closure.are_equal(f_a, f_b));
  closure.assert_equal(a_term, b_term, {primo::sat::Lit{-4}});
  check(closure.are_equal(f_a, f_b));
  check(closure.stats().backtracks == 1);
}

void flat_terms_curry_nary_applications_deterministically() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto g = symbols.declare_function("g", {sort, sort}, sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto g_ab = terms.mk_app(g, {a_term, b_term});

  FlatTermIndex flat(terms);
  const auto first = flat.flatten(g_ab);
  const auto second = flat.flatten(g_ab);

  check(first == second);
  check(flat.definitions().size() == 2);
  check(flat.existing_flat_term(g_ab).value() == first);
}

void flat_terms_share_nested_subterms_and_flatten_equalities() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto g = symbols.declare_function("g", {sort, sort}, sort);
  const auto h = symbols.declare_function("h", {sort, sort}, sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto g_ab = terms.mk_app(g, {a_term, b_term});
  const auto h_shared = terms.mk_app(h, {g_ab, g_ab});
  const auto eq = terms.mk_eq(h_shared, g_ab);

  FlatTermIndex flat(terms);
  const auto flat_eq = flat.flatten_equality_atom(eq);

  check(flat_eq.has_value());
  check(flat_eq->lhs == flat.existing_flat_term(h_shared).value());
  check(flat_eq->rhs == flat.existing_flat_term(g_ab).value());
  check(flat.definitions().size() == 4);
}

void flat_congruence_closure_uses_curried_definitions() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto c = symbols.declare_constant("c", sort);
  const auto g = symbols.declare_function("g", {sort, sort}, sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto c_term = terms.mk_constant(c);
  const auto g_a_c = terms.mk_app(g, {a_term, c_term});
  const auto g_b_c = terms.mk_app(g, {b_term, c_term});

  FlatTermIndex flat(terms);
  const auto flat_g_a_c = flat.flatten(g_a_c);
  const auto flat_g_b_c = flat.flatten(g_b_c);
  const auto flat_a = flat.existing_flat_term(a_term).value();
  const auto flat_b = flat.existing_flat_term(b_term).value();

  FlatCongruenceClosure closure(flat.size());
  for (const auto& definition : flat.definitions()) {
    closure.add_definition(definition);
  }
  closure.assert_equal(flat_a, flat_b, {primo::sat::Lit{-5}});

  check(closure.are_equal(flat_g_a_c, flat_g_b_c));
  check(closure.stats().lookup_hits >= 1);
  check(closure.stats().congruence_merges >= 1);
}

void flat_congruence_closure_drains_changed_terms_from_merges() {
  FlatCongruenceClosure closure(4);
  std::vector<primo::theory::equality::FlatTermId> changed;

  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
  closure.drain_changed_terms(changed);
  check(changed.size() == 1);
  check(changed.front() == 0 || changed.front() == 1);
  changed.clear();

  const std::size_t checkpoint = closure.push();
  closure.assert_equal(2, 3, {primo::sat::Lit{-2}});
  closure.pop(checkpoint);
  closure.drain_changed_terms(changed);
  check(changed.empty());
}

void flat_congruence_closure_skips_unrelated_disequality_scans() {
  FlatCongruenceClosure closure(6);
  closure.assert_distinct(0, 1, {primo::sat::Lit{-1}});
  closure.assert_distinct(2, 3, {primo::sat::Lit{-2}});
  check(closure.stats().disequality_checks == 2);

  closure.assert_equal(4, 5, {primo::sat::Lit{-3}});
  check(!closure.conflict().has_value());
  check(closure.stats().disequality_checks == 2);

  closure.assert_equal(0, 1, {primo::sat::Lit{-4}});
  const auto conflict = closure.conflict();
  check(conflict.has_value());
  check(closure.stats().disequality_checks == 3);
  check(closure.stats().conflicts == 1);
}

void flat_congruence_closure_scans_only_crossing_disequalities() {
  FlatCongruenceClosure closure(6);
  closure.assert_distinct(0, 2, {primo::sat::Lit{-1}});
  closure.assert_distinct(1, 3, {primo::sat::Lit{-2}});
  closure.assert_distinct(4, 5, {primo::sat::Lit{-3}});
  check(closure.stats().disequality_checks == 3);

  closure.assert_equal(0, 1, {primo::sat::Lit{-4}});
  check(!closure.conflict().has_value());
  check(closure.stats().disequality_checks == 3);

  closure.assert_equal(2, 3, {primo::sat::Lit{-5}});
  check(!closure.conflict().has_value());
  check(closure.stats().disequality_checks == 3);

  closure.assert_equal(0, 2, {primo::sat::Lit{-6}});
  check(closure.conflict().has_value());
  check(closure.stats().disequality_checks == 4);
}

void flat_congruence_closure_keeps_signed_reason_literals_distinct() {
  FlatCongruenceClosure closure(3);
  closure.assert_equal(0, 1, {primo::sat::Lit{1}});
  closure.assert_equal(1, 2, {primo::sat::Lit{-1}});

  const auto reason = closure.explain_equal(0, 2);
  check(reason.has_value());
  check(reason->size() == 2);

  bool has_positive = false;
  bool has_negative = false;
  for (const primo::sat::Lit lit : reason.value()) {
    has_positive = has_positive || lit.value == 1;
    has_negative = has_negative || lit.value == -1;
  }
  check(has_positive);
  check(has_negative);
}

void flat_congruence_closure_reuses_repeated_sub_explanations() {
  primo::core::SymbolTable symbols;
  const auto sort = symbols.declare_sort("A");
  const auto a = symbols.declare_constant("a", sort);
  const auto b = symbols.declare_constant("b", sort);
  const auto h = symbols.declare_function("h", {sort, sort}, sort);

  primo::core::TermManager terms(symbols);
  const auto a_term = terms.mk_constant(a);
  const auto b_term = terms.mk_constant(b);
  const auto h_aa = terms.mk_app(h, {a_term, a_term});
  const auto h_bb = terms.mk_app(h, {b_term, b_term});

  FlatTermIndex flat(terms);
  const auto flat_h_aa = flat.flatten(h_aa);
  const auto flat_h_bb = flat.flatten(h_bb);
  const auto flat_a = flat.existing_flat_term(a_term).value();
  const auto flat_b = flat.existing_flat_term(b_term).value();

  FlatCongruenceClosure closure(flat.size());
  for (const auto& definition : flat.definitions()) {
    closure.add_definition(definition);
  }
  closure.assert_equal(flat_a, flat_b, {primo::sat::Lit{-8}});

  const auto reason = closure.explain_equal(flat_h_aa, flat_h_bb);
  check(reason.has_value());
  check(reason->size() == 1);
  check(reason->front().value == -8);
  check(closure.stats().explain_cache_hits >= 1);
}

void flat_congruence_closure_backtracks_and_rediscoveres_congruence() {
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

  FlatTermIndex flat(terms);
  const auto flat_f_a = flat.flatten(f_a);
  const auto flat_f_b = flat.flatten(f_b);
  const auto flat_a = flat.existing_flat_term(a_term).value();
  const auto flat_b = flat.existing_flat_term(b_term).value();

  FlatCongruenceClosure closure(flat.size());
  for (const auto& definition : flat.definitions()) {
    closure.add_definition(definition);
  }

  const std::size_t checkpoint = closure.push();
  closure.assert_equal(flat_a, flat_b, {primo::sat::Lit{-6}});
  check(closure.are_equal(flat_f_a, flat_f_b));
  closure.pop(checkpoint);
  check(!closure.are_equal(flat_f_a, flat_f_b));
  closure.assert_equal(flat_a, flat_b, {primo::sat::Lit{-7}});
  check(closure.are_equal(flat_f_a, flat_f_b));
  check(closure.stats().backtracks == 1);
}

void flat_congruence_closure_backtracks_nested_merges() {
  FlatCongruenceClosure closure(4);

  const std::size_t outer = closure.push();
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
  check(closure.are_equal(0, 1));

  const std::size_t inner = closure.push();
  closure.assert_equal(1, 2, {primo::sat::Lit{-2}});
  check(closure.are_equal(0, 2));

  closure.pop(inner);
  check(closure.are_equal(0, 1));
  check(!closure.are_equal(0, 2));

  closure.pop(outer);
  check(!closure.are_equal(0, 1));
  check(!closure.are_equal(1, 2));
  check(closure.stats().backtracks == 1);
}

void flat_congruence_closure_backtracks_disequality_conflict() {
  FlatCongruenceClosure closure(2);

  const std::size_t checkpoint = closure.push();
  closure.assert_distinct(0, 1, {primo::sat::Lit{-3}});
  closure.assert_equal(0, 1, {primo::sat::Lit{4}});
  check(closure.conflict().has_value());

  closure.pop(checkpoint);
  check(!closure.conflict().has_value());
  closure.assert_equal(0, 1, {primo::sat::Lit{5}});
  check(!closure.conflict().has_value());
  check(closure.are_equal(0, 1));
}

void flat_congruence_closure_backtracks_post_checkpoint_definitions() {
  FlatCongruenceClosure closure(4);

  const std::size_t checkpoint = closure.push();
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 2});
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 3});
  check(closure.are_equal(2, 3));

  closure.pop(checkpoint);
  check(!closure.are_equal(2, 3));
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 2});
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 3});
  check(closure.are_equal(2, 3));
}

}  // namespace

int main() {
  congruent_applications_are_merged();
  distinct_unrelated_terms_are_satisfiable();
  explained_closure_explains_explicit_chain();
  explained_closure_explains_congruence();
  incremental_closure_discovers_unary_congruence_with_lookup();
  incremental_closure_discovers_binary_shared_subterm_congruence();
  incremental_closure_reports_disequality_conflict();
  incremental_closure_backtracks_and_rediscoveres_congruence();
  flat_terms_curry_nary_applications_deterministically();
  flat_terms_share_nested_subterms_and_flatten_equalities();
  flat_congruence_closure_uses_curried_definitions();
  flat_congruence_closure_drains_changed_terms_from_merges();
  flat_congruence_closure_skips_unrelated_disequality_scans();
  flat_congruence_closure_scans_only_crossing_disequalities();
  flat_congruence_closure_keeps_signed_reason_literals_distinct();
  flat_congruence_closure_reuses_repeated_sub_explanations();
  flat_congruence_closure_backtracks_and_rediscoveres_congruence();
  flat_congruence_closure_backtracks_nested_merges();
  flat_congruence_closure_backtracks_disequality_conflict();
  flat_congruence_closure_backtracks_post_checkpoint_definitions();
  return 0;
}
