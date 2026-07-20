#include <cstddef>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

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
using primo::theory::equality::FlatCongruenceClosureTestAccess;
using primo::theory::equality::FlatExplanationSearchMode;
using primo::theory::equality::FlatTermIndex;
using primo::theory::equality::IncrementalCongruenceClosure;

namespace primo::theory::equality {
class FlatCongruenceClosureTestAccess {
 public:
  static bool explain_pair_with_bound(FlatCongruenceClosure& closure, FlatTermId lhs,
                                      FlatTermId rhs, std::vector<primo::sat::Lit>& reason,
                                      std::size_t max_edge_index) {
    return closure.explain_equal_into(lhs, rhs, reason, max_edge_index);
  }

  static void begin_reason(FlatCongruenceClosure& closure, std::vector<primo::sat::Lit>& reason) {
    closure.begin_reason_collection(reason);
  }
};
}  // namespace primo::theory::equality

namespace {

void check(bool condition) {
  if (!condition) {
    std::abort();
  }
}

class NaiveFlatOracle {
 public:
  explicit NaiveFlatOracle(std::size_t node_count) : parent_(node_count), size_(node_count, 1) {
    for (std::size_t i = 0; i < node_count; ++i) {
      parent_[i] = i;
    }
  }

  std::size_t push() {
    snapshots_.push_back(Snapshot{parent_, size_});
    return snapshots_.size() - 1;
  }

  void pop(std::size_t checkpoint) {
    const Snapshot snapshot = snapshots_.at(checkpoint);
    snapshots_.resize(checkpoint);
    parent_ = snapshot.parent;
    size_ = snapshot.size;
  }

  void add_definition(primo::theory::equality::FlatDefinition definition) {
    definitions_.push_back(definition);
    saturate();
  }

  void assert_equal(std::size_t lhs, std::size_t rhs) {
    merge(lhs, rhs);
    saturate();
  }

  bool are_equal(std::size_t lhs, std::size_t rhs) const { return find(lhs) == find(rhs); }

 private:
  struct Snapshot {
    std::vector<std::size_t> parent;
    std::vector<std::size_t> size;
  };

  std::size_t find(std::size_t value) const {
    while (parent_[value] != value) {
      value = parent_[value];
    }
    return value;
  }

  bool merge(std::size_t lhs, std::size_t rhs) {
    std::size_t lhs_repr = find(lhs);
    std::size_t rhs_repr = find(rhs);
    if (lhs_repr == rhs_repr) {
      return false;
    }
    if (size_[lhs_repr] > size_[rhs_repr]) {
      std::swap(lhs_repr, rhs_repr);
    }
    parent_[lhs_repr] = rhs_repr;
    size_[rhs_repr] += size_[lhs_repr];
    size_[lhs_repr] = 0;
    return true;
  }

  void saturate() {
    bool changed = true;
    while (changed) {
      changed = false;
      for (std::size_t i = 0; i < definitions_.size(); ++i) {
        for (std::size_t j = i + 1; j < definitions_.size(); ++j) {
          const auto& lhs = definitions_[i];
          const auto& rhs = definitions_[j];
          if (find(lhs.function) == find(rhs.function) &&
              find(lhs.argument) == find(rhs.argument)) {
            changed = merge(lhs.result, rhs.result) || changed;
          }
        }
      }
    }
  }

  std::vector<std::size_t> parent_;
  std::vector<std::size_t> size_;
  std::vector<primo::theory::equality::FlatDefinition> definitions_;
  std::vector<Snapshot> snapshots_;
};

bool explanation_entails_equality(
    std::size_t node_count, const std::vector<primo::theory::equality::FlatDefinition>& definitions,
    const std::vector<std::pair<std::size_t, std::size_t>>& labelled_equalities,
    const std::vector<primo::sat::Lit>& reason, std::size_t lhs, std::size_t rhs) {
  NaiveFlatOracle oracle(node_count);
  for (const auto& definition : definitions) {
    oracle.add_definition(definition);
  }
  for (const primo::sat::Lit lit : reason) {
    if (lit.value >= 0) {
      continue;
    }
    const std::size_t index = static_cast<std::size_t>(-lit.value);
    if (index == 0 || index > labelled_equalities.size()) {
      continue;
    }
    const auto& equality = labelled_equalities[index - 1];
    oracle.assert_equal(equality.first, equality.second);
  }
  return oracle.are_equal(lhs, rhs);
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
  check(closure.stats().conflict_reason_max_length == 2);
  check(closure.stats().conflict_reason_length_le_4 == 1);
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
  check(closure.stats().conflict_reason_length_le_4 == 1);
}

void flat_congruence_closure_reports_direct_conflict_before_use_list_scan() {
  FlatCongruenceClosure closure(8);
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 2, 4});
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 3, 5});
  closure.add_definition(primo::theory::equality::FlatDefinition{1, 2, 6});
  closure.add_definition(primo::theory::equality::FlatDefinition{1, 3, 7});
  const std::size_t checkpoint = closure.push();
  closure.assert_distinct(0, 1, {primo::sat::Lit{-1}});
  const std::size_t lookup_probes_before_conflict = closure.stats().lookup_probes;

  closure.assert_equal(0, 1, {primo::sat::Lit{-2}});

  check(closure.conflict().has_value());
  check(closure.stats().conflicts == 1);
  check(closure.stats().lookup_probes == lookup_probes_before_conflict);

  closure.pop(checkpoint);
  closure.assert_equal(0, 1, {primo::sat::Lit{-3}});
  check(!closure.conflict().has_value());
  check(closure.are_equal(0, 1));
}

void flat_congruence_closure_keeps_signed_reason_literals_distinct() {
  FlatCongruenceClosure closure(3);
  closure.assert_equal(0, 1, {primo::sat::Lit{1}});
  closure.assert_equal(1, 2, {primo::sat::Lit{-1}});

  const auto reason = closure.explain_equal(0, 2);
  check(reason.has_value());
  check(reason->size() == 2);
  check(closure.stats().explain_bfs_searches >= 1);
  check(closure.stats().explain_bfs_visited >= 1);
  check(closure.stats().explain_bfs_path_edges >= 1);
  check(closure.stats().explain_bfs_max_queue >= 1);

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
  check(closure.stats().explain_top_level_queries >= 1);
  check(closure.stats().explain_recursive_queries >= 1);
  check(closure.stats().explain_repeated_pair_queries >= 1);
  check(closure.stats().explain_max_recursion_depth >= 1);
  check(closure.stats().explain_bfs_searches >= 1);
  check(closure.stats().explain_bfs_visited >= 1);
  check(closure.stats().explain_bfs_path_edges >= 1);
  check(closure.stats().explain_bfs_max_queue >= 1);
  check(closure.stats().explain_direct_edges >= 1);
  check(closure.stats().explain_congruence_edges >= 1);
}

void flat_congruence_closure_uses_causal_edges_for_congruence_reasons() {
  FlatCongruenceClosure closure(11);
  closure.add_definition(primo::theory::equality::FlatDefinition{10, 0, 4});
  closure.add_definition(primo::theory::equality::FlatDefinition{10, 2, 5});

  closure.assert_equal(0, 6, {primo::sat::Lit{-1}});
  closure.assert_equal(6, 7, {primo::sat::Lit{-2}});
  closure.assert_equal(7, 2, {primo::sat::Lit{-3}});
  check(closure.are_equal(4, 5));

  closure.assert_equal(6, 4, {primo::sat::Lit{-4}});
  const auto reason = closure.explain_equal(4, 5);

  check(reason.has_value());
  bool uses_later_edge = false;
  for (const primo::sat::Lit lit : reason.value()) {
    uses_later_edge = uses_later_edge || lit.value == -4;
  }
  check(!uses_later_edge);
}

void flat_congruence_closure_bidirectional_uses_causal_edges_for_congruence_reasons() {
  FlatCongruenceClosure closure(11, FlatExplanationSearchMode::BidirectionalBfs);
  closure.add_definition(primo::theory::equality::FlatDefinition{10, 0, 4});
  closure.add_definition(primo::theory::equality::FlatDefinition{10, 2, 5});

  closure.assert_equal(0, 6, {primo::sat::Lit{-1}});
  closure.assert_equal(6, 7, {primo::sat::Lit{-2}});
  closure.assert_equal(7, 2, {primo::sat::Lit{-3}});
  check(closure.are_equal(4, 5));

  closure.assert_equal(6, 4, {primo::sat::Lit{-4}});
  const auto reason = closure.explain_equal(4, 5);

  check(reason.has_value());
  bool uses_later_edge = false;
  for (const primo::sat::Lit lit : reason.value()) {
    uses_later_edge = uses_later_edge || lit.value == -4;
  }
  check(!uses_later_edge);
  check(closure.stats().explain_bfs_searches >= 1);
}

void flat_congruence_closure_explanation_cache_respects_edge_bounds() {
  FlatCongruenceClosure closure(3);
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
  closure.assert_equal(1, 2, {primo::sat::Lit{-2}});

  std::vector<primo::sat::Lit> reason;
  FlatCongruenceClosureTestAccess::begin_reason(closure, reason);
  check(FlatCongruenceClosureTestAccess::explain_pair_with_bound(closure, 0, 2, reason, 2));
  const std::size_t bfs_after_broad_query = closure.stats().explain_bfs_searches;
  check(!FlatCongruenceClosureTestAccess::explain_pair_with_bound(closure, 0, 2, reason, 1));
  check(closure.stats().explain_cache_bound_misses >= 1);
  check(closure.stats().explain_bfs_searches > bfs_after_broad_query);
}

void flat_congruence_closure_bidirectional_explanation_cache_respects_edge_bounds() {
  FlatCongruenceClosure closure(3, FlatExplanationSearchMode::BidirectionalBfs);
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
  closure.assert_equal(1, 2, {primo::sat::Lit{-2}});

  std::vector<primo::sat::Lit> reason;
  FlatCongruenceClosureTestAccess::begin_reason(closure, reason);
  check(FlatCongruenceClosureTestAccess::explain_pair_with_bound(closure, 0, 2, reason, 2));
  const std::size_t bfs_after_broad_query = closure.stats().explain_bfs_searches;
  check(!FlatCongruenceClosureTestAccess::explain_pair_with_bound(closure, 0, 2, reason, 1));
  check(closure.stats().explain_cache_bound_misses >= 1);
  check(closure.stats().explain_bfs_searches > bfs_after_broad_query);
}

void flat_congruence_closure_stops_at_chronological_explanation_bound() {
  FlatCongruenceClosure closure(4);
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
  closure.assert_equal(0, 2, {primo::sat::Lit{-2}});
  closure.assert_equal(0, 3, {primo::sat::Lit{-3}});

  std::vector<primo::sat::Lit> reason;
  FlatCongruenceClosureTestAccess::begin_reason(closure, reason);
  check(!FlatCongruenceClosureTestAccess::explain_pair_with_bound(closure, 0, 2, reason, 1));
  check(closure.stats().explain_causal_edge_skips == 1);
}

void flat_congruence_closure_bidirectional_backtracks_nested_merges() {
  FlatCongruenceClosure closure(4, FlatExplanationSearchMode::BidirectionalBfs);

  const std::size_t outer = closure.push();
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
  check(closure.are_equal(0, 1));

  const std::size_t inner = closure.push();
  closure.assert_equal(1, 2, {primo::sat::Lit{-2}});
  check(closure.are_equal(0, 2));
  const auto reason = closure.explain_equal(0, 2);
  check(reason.has_value());
  check(closure.stats().explain_bfs_searches >= 1);

  closure.pop(inner);
  check(closure.are_equal(0, 1));
  check(!closure.are_equal(0, 2));

  closure.pop(outer);
  check(!closure.are_equal(0, 1));
  check(!closure.are_equal(1, 2));
  check(closure.stats().backtracks == 1);
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

void flat_congruence_closure_proof_diagnostics_backtrack() {
  FlatCongruenceClosure closure(4, FlatExplanationSearchMode::OneSidedBfs, true);
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
  const std::size_t checkpoint = closure.push();
  closure.assert_equal(2, 3, {primo::sat::Lit{-2}});
  closure.assert_equal(1, 2, {primo::sat::Lit{-3}});

  const auto before_pop = closure.explain_equal(0, 3);
  check(before_pop.has_value());
  check(closure.stats().explain_proof_forest_queries >= 1);
  check(closure.stats().explain_proof_forest_path_edges >= 1);
  check(closure.stats().explain_proof_forest_missing_lca == 0);
  check(closure.stats().explain_proof_forest_shorter_than_bfs +
            closure.stats().explain_proof_forest_equal_to_bfs +
            closure.stats().explain_proof_forest_longer_than_bfs >=
        1);

  closure.pop(checkpoint);
  check(!closure.are_equal(0, 3));
  const auto after_pop = closure.explain_equal(0, 1);
  check(after_pop.has_value());
  check(closure.stats().explain_proof_forest_queries >= 1);
  check(closure.stats().explain_proof_forest_missing_lca == 0);
}

void flat_congruence_closure_proof_diagnostics_respect_causal_bounds() {
  FlatCongruenceClosure closure(3, FlatExplanationSearchMode::OneSidedBfs, true);
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});
  closure.assert_equal(1, 2, {primo::sat::Lit{-2}});

  std::vector<primo::sat::Lit> reason;
  FlatCongruenceClosureTestAccess::begin_reason(closure, reason);
  check(FlatCongruenceClosureTestAccess::explain_pair_with_bound(closure, 0, 2, reason, 2));
  const std::size_t missing_lca_before = closure.stats().explain_proof_forest_missing_lca;

  check(!FlatCongruenceClosureTestAccess::explain_pair_with_bound(closure, 0, 2, reason, 1));
  check(closure.stats().explain_proof_forest_missing_lca > missing_lca_before);
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

void flat_congruence_closure_conflict_reason_entails_nested_congruence() {
  constexpr std::size_t kNodeCount = 8;
  const std::vector<primo::theory::equality::FlatDefinition> definitions{
      {4, 0, 2},
      {4, 1, 3},
      {5, 2, 6},
      {5, 3, 7},
  };
  FlatCongruenceClosure closure(8);
  for (const auto& definition : definitions) {
    closure.add_definition(definition);
  }

  const std::size_t checkpoint = closure.push();
  closure.assert_distinct(6, 7, {primo::sat::Lit{-2}});
  closure.assert_equal(0, 1, {primo::sat::Lit{-1}});

  const auto conflict = closure.conflict();
  check(conflict.has_value());
  check(explanation_entails_equality(kNodeCount, definitions, {{0, 1}}, conflict.value(), 6, 7));
  bool has_disequality_literal = false;
  for (const primo::sat::Lit lit : conflict.value()) {
    has_disequality_literal = has_disequality_literal || lit.value == -2;
  }
  check(has_disequality_literal);

  closure.pop(checkpoint);
  check(!closure.conflict().has_value());
  check(!closure.are_equal(6, 7));
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

void flat_congruence_closure_lookup_hit_definition_keeps_future_congruence() {
  FlatCongruenceClosure closure(8);

  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 4});
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 5});
  check(closure.are_equal(4, 5));

  closure.add_definition(primo::theory::equality::FlatDefinition{2, 1, 6});
  check(!closure.are_equal(5, 6));

  closure.assert_equal(0, 2, {primo::sat::Lit{-1}});
  check(closure.are_equal(4, 6));
  check(closure.are_equal(5, 6));
  check(closure.stats().lookup_hits >= 2);
  check(closure.stats().congruence_merges >= 2);
}

void flat_congruence_closure_rejects_conflicting_result_definitions() {
  FlatCongruenceClosure closure(5);
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 2});
  const std::size_t definitions_after_first = closure.stats().flat_definitions;

  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 2});
  check(closure.stats().flat_definitions == definitions_after_first);

  bool rejected = false;
  try {
    closure.add_definition(primo::theory::equality::FlatDefinition{3, 4, 2});
  } catch (const std::logic_error&) {
    rejected = true;
  }
  check(rejected);
}

void flat_congruence_closure_deduplicates_affected_use_lists_after_backtrack() {
  FlatCongruenceClosure closure(8);
  closure.add_definition(primo::theory::equality::FlatDefinition{0, 1, 2});
  const std::size_t checkpoint = closure.push();

  closure.assert_equal(0, 1);
  check(closure.stats().use_list_appends >= 3);
  closure.assert_equal(3, 4);
  closure.assert_equal(3, 5);
  closure.assert_equal(1, 3);
  check(closure.stats().affected_use_list_dedup >= 1);
  check(!closure.conflict().has_value());

  closure.pop(checkpoint);
  check(!closure.are_equal(0, 1));
  closure.assert_equal(0, 1);
  check(!closure.conflict().has_value());
  check(closure.are_equal(0, 1));
}

void flat_congruence_closure_matches_naive_oracle_under_backtracking() {
  constexpr std::size_t kNodeCount = 30;
  FlatCongruenceClosure closure(kNodeCount);
  NaiveFlatOracle oracle(kNodeCount);
  const std::vector<primo::theory::equality::FlatDefinition> definitions{
      {6, 0, 9},   {6, 1, 10},  {6, 2, 11},  {6, 3, 12},  {6, 4, 13},  {6, 5, 14},  {7, 0, 15},
      {7, 1, 16},  {7, 2, 17},  {7, 3, 18},  {7, 4, 19},  {7, 5, 20},  {8, 9, 21},  {8, 10, 22},
      {8, 11, 23}, {8, 12, 24}, {8, 13, 25}, {8, 14, 26}, {8, 15, 27}, {8, 16, 28}, {8, 17, 29},
  };
  for (const auto& definition : definitions) {
    closure.add_definition(definition);
    oracle.add_definition(definition);
  }

  std::vector<std::size_t> closure_checkpoints;
  std::vector<std::size_t> oracle_checkpoints;
  std::size_t state = 0x9e3779b97f4a7c15ULL;
  auto next = [&state]() {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return state;
  };
  auto compare_all_pairs = [&]() {
    for (std::size_t lhs = 0; lhs < kNodeCount; ++lhs) {
      for (std::size_t rhs = 0; rhs < kNodeCount; ++rhs) {
        check(closure.are_equal(lhs, rhs) == oracle.are_equal(lhs, rhs));
      }
    }
  };

  compare_all_pairs();
  for (std::size_t step = 0; step < 220; ++step) {
    const std::size_t choice = next() % 11;
    if (choice == 0 && closure_checkpoints.size() < 8) {
      closure_checkpoints.push_back(closure.push());
      oracle_checkpoints.push_back(oracle.push());
    } else if (choice == 1 && !closure_checkpoints.empty()) {
      const std::size_t index = next() % closure_checkpoints.size();
      closure.pop(closure_checkpoints[index]);
      oracle.pop(oracle_checkpoints[index]);
      closure_checkpoints.resize(index);
      oracle_checkpoints.resize(index);
    } else {
      const std::size_t lhs = next() % 18;
      const std::size_t rhs = next() % 18;
      closure.assert_equal(lhs, rhs, {primo::sat::Lit{-static_cast<int>(step + 1)}});
      oracle.assert_equal(lhs, rhs);
    }
    compare_all_pairs();
  }
}

void flat_congruence_closure_explanations_match_naive_oracle_under_backtracking() {
  constexpr std::size_t kNodeCount = 30;
  FlatCongruenceClosure closure(kNodeCount);
  const std::vector<primo::theory::equality::FlatDefinition> definitions{
      {6, 0, 9},   {6, 1, 10},  {6, 2, 11},  {6, 3, 12},  {6, 4, 13},  {6, 5, 14},  {7, 0, 15},
      {7, 1, 16},  {7, 2, 17},  {7, 3, 18},  {7, 4, 19},  {7, 5, 20},  {8, 9, 21},  {8, 10, 22},
      {8, 11, 23}, {8, 12, 24}, {8, 13, 25}, {8, 14, 26}, {8, 15, 27}, {8, 16, 28}, {8, 17, 29},
  };
  for (const auto& definition : definitions) {
    closure.add_definition(definition);
  }

  struct Snapshot {
    std::vector<std::pair<std::size_t, std::size_t>> labelled_equalities;
  };
  std::vector<std::size_t> closure_checkpoints;
  std::vector<Snapshot> snapshots;
  std::vector<std::pair<std::size_t, std::size_t>> labelled_equalities;
  std::size_t state = 0x517cc1b727220a95ULL;
  auto next = [&state]() {
    state = state * 2862933555777941757ULL + 3037000493ULL;
    return state;
  };
  auto check_explanations = [&]() {
    for (std::size_t lhs = 9; lhs < kNodeCount; ++lhs) {
      for (std::size_t rhs = lhs + 1; rhs < kNodeCount; ++rhs) {
        const bool equal = closure.are_equal(lhs, rhs);
        const std::optional<std::vector<primo::sat::Lit>> reason = closure.explain_equal(lhs, rhs);
        check(reason.has_value() == equal);
        if (reason.has_value()) {
          check(explanation_entails_equality(kNodeCount, definitions, labelled_equalities,
                                             reason.value(), lhs, rhs));
        }
      }
    }
  };

  for (std::size_t step = 0; step < 180; ++step) {
    const std::size_t choice = next() % 10;
    if (choice == 0 && closure_checkpoints.size() < 8) {
      closure_checkpoints.push_back(closure.push());
      snapshots.push_back(Snapshot{labelled_equalities});
    } else if (choice == 1 && !closure_checkpoints.empty()) {
      const std::size_t index = next() % closure_checkpoints.size();
      closure.pop(closure_checkpoints[index]);
      labelled_equalities = snapshots[index].labelled_equalities;
      closure_checkpoints.resize(index);
      snapshots.resize(index);
    } else {
      const std::size_t lhs = next() % 18;
      const std::size_t rhs = next() % 18;
      labelled_equalities.emplace_back(lhs, rhs);
      closure.assert_equal(lhs, rhs,
                           {primo::sat::Lit{-static_cast<int>(labelled_equalities.size())}});
    }
    check_explanations();
  }
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
  flat_congruence_closure_reports_direct_conflict_before_use_list_scan();
  flat_congruence_closure_keeps_signed_reason_literals_distinct();
  flat_congruence_closure_reuses_repeated_sub_explanations();
  flat_congruence_closure_uses_causal_edges_for_congruence_reasons();
  flat_congruence_closure_bidirectional_uses_causal_edges_for_congruence_reasons();
  flat_congruence_closure_explanation_cache_respects_edge_bounds();
  flat_congruence_closure_bidirectional_explanation_cache_respects_edge_bounds();
  flat_congruence_closure_stops_at_chronological_explanation_bound();
  flat_congruence_closure_bidirectional_backtracks_nested_merges();
  flat_congruence_closure_backtracks_and_rediscoveres_congruence();
  flat_congruence_closure_backtracks_nested_merges();
  flat_congruence_closure_proof_diagnostics_backtrack();
  flat_congruence_closure_proof_diagnostics_respect_causal_bounds();
  flat_congruence_closure_backtracks_disequality_conflict();
  flat_congruence_closure_conflict_reason_entails_nested_congruence();
  flat_congruence_closure_backtracks_post_checkpoint_definitions();
  flat_congruence_closure_lookup_hit_definition_keeps_future_congruence();
  flat_congruence_closure_rejects_conflicting_result_definitions();
  flat_congruence_closure_deduplicates_affected_use_lists_after_backtrack();
  flat_congruence_closure_matches_naive_oracle_under_backtracking();
  flat_congruence_closure_explanations_match_naive_oracle_under_backtracking();
  return 0;
}
