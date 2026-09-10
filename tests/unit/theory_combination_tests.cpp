#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "sat/lit.hpp"
#include "smt/model_based_combination.hpp"
#include "smt/theory_combination.hpp"
#include "theory/theory_solver.hpp"
#include "util/rational.hpp"

using primo::core::SymbolTable;
using primo::core::TermManager;
using primo::smt::analyze_nelson_oppen_interface;
using primo::smt::InterfaceEqualityAssignment;
using primo::smt::InterfaceEqualityClassification;
using primo::smt::InterfaceEqualityStatus;
using primo::smt::InterfaceEqualityValue;
using primo::smt::ModelBasedCombinationCoordinator;
using primo::smt::ModelBasedCombinationStatus;
using primo::smt::ModelBasedComponentRound;
using primo::smt::PurificationSiteKind;
using primo::smt::SharedEqualitySource;
using primo::smt::SharedInterfaceTerms;
using primo::smt::SharedModelEquality;
using primo::smt::TheoryCombinationManager;
using primo::theory::AssignedTheoryLiteral;
using primo::theory::Convexity;
using primo::theory::IncrementalTheorySolver;
using primo::theory::InterfacePair;
using primo::theory::InterfaceRelation;
using primo::theory::InterfaceRelationValue;
using primo::theory::SharedModelClass;
using primo::theory::SharedModelPartition;
using primo::theory::StableInfiniteness;
using primo::theory::TheoryCheckEffort;
using primo::theory::TheoryCheckResult;
using primo::theory::TheoryCheckStatus;
using primo::theory::TheoryModelProjection;
using primo::theory::TheoryModelToken;
using primo::theory::TheoryMutationRequest;
using primo::theory::TheoryMutationResult;
using primo::theory::TheoryMutationStatus;
using primo::theory::TheoryProperties;
using primo::theory::TheorySolver;

namespace {

struct MockTheoryDispatch {
  std::size_t visible_pairs = 0;
  std::size_t missing_pairs = 0;
  std::vector<primo::core::TermId> visible_atoms;

  void inspect(const primo::smt::NelsonOppenInterfaceSummary& summary) {
    for (const auto& entry : summary.equality_registry) {
      if (entry.status == InterfaceEqualityStatus::Visible) {
        ++visible_pairs;
        visible_atoms.push_back(entry.atom);
        assert(entry.atom != primo::core::kInvalidTermId);
      } else {
        ++missing_pairs;
        assert(entry.atom == primo::core::kInvalidTermId);
      }
    }
  }
};

class MockTheorySolver final : public TheorySolver {
 public:
  explicit MockTheorySolver(TheoryProperties properties) : properties_(std::move(properties)) {}

  const TheoryProperties& properties() const override { return properties_; }

  TheoryCheckResult check(std::span<const AssignedTheoryLiteral> assignment,
                          std::span<const InterfaceRelation> arrangement) override {
    if (throw_on_check) {
      throw std::runtime_error("scripted component failure");
    }
    assigned_count = assignment.size();
    arrangement_count = arrangement.size();
    return next_result;
  }

  TheoryMutationResult mutate(const TheoryMutationRequest& request) override {
    ++mutation_count;
    last_mutation_source = request.source;
    return next_mutation;
  }

  std::vector<InterfacePair> care_pairs(const TheoryModelToken&) const override {
    return next_care_pairs;
  }

  TheoryProperties properties_;
  TheoryCheckResult next_result{.status = TheoryCheckStatus::Consistent,
                                .clause = {},
                                .model = {},
                                .entailed_lit = {},
                                .reasons = {},
                                .decision = {},
                                .diagnostic = {}};
  TheoryMutationResult next_mutation{};
  std::vector<InterfacePair> next_care_pairs;
  TheoryModelToken last_mutation_source;
  std::size_t assigned_count = 0;
  std::size_t arrangement_count = 0;
  std::size_t mutation_count = 0;
  bool throw_on_check = false;
};

class MockIncrementalTheorySolver final : public IncrementalTheorySolver {
 public:
  explicit MockIncrementalTheorySolver(TheoryProperties properties)
      : properties_(std::move(properties)) {}

  const TheoryProperties& properties() const override { return properties_; }

  void reset() override {
    trail_.clear();
    level_starts_.clear();
  }

  void push_level() override { level_starts_.push_back(trail_.size()); }

  void pop_to_level(std::size_t level) override {
    assert(level <= level_starts_.size());
    const std::size_t keep = level < level_starts_.size() ? level_starts_[level] : trail_.size();
    trail_.resize(keep);
    level_starts_.resize(level);
  }

  void assign(std::span<const AssignedTheoryLiteral> assignment_delta) override {
    trail_.insert(trail_.end(), assignment_delta.begin(), assignment_delta.end());
  }

  TheoryCheckResult check(TheoryCheckEffort effort) override {
    last_effort = effort;
    checked_sizes.push_back(trail_.size());
    return next_result;
  }

  TheoryProperties properties_;
  std::vector<AssignedTheoryLiteral> trail_;
  std::vector<std::size_t> level_starts_;
  std::vector<std::size_t> checked_sizes;
  TheoryCheckEffort last_effort = TheoryCheckEffort::Partial;
  TheoryCheckResult next_result{.status = TheoryCheckStatus::Consistent,
                                .clause = {},
                                .model = {},
                                .entailed_lit = {},
                                .reasons = {},
                                .decision = {},
                                .diagnostic = {}};
};

TheoryModelProjection projection(std::string component, std::uint64_t epoch, std::uint64_t revision,
                                 primo::core::SortId sort,
                                 std::vector<std::vector<primo::core::TermId>> classes) {
  SharedModelPartition partition{.sort = sort, .equality_classes = {}};
  partition.equality_classes.reserve(classes.size());
  for (auto& terms : classes) {
    partition.equality_classes.push_back(SharedModelClass{.terms = std::move(terms)});
  }
  return TheoryModelProjection{.token = TheoryModelToken{.component = std::move(component),
                                                         .assignment_epoch = epoch,
                                                         .revision = revision},
                               .partitions = {std::move(partition)}};
}

TheoryCheckResult consistent_with(TheoryModelProjection model) {
  return TheoryCheckResult{.status = TheoryCheckStatus::Consistent,
                           .clause = {},
                           .model = std::move(model),
                           .entailed_lit = {},
                           .reasons = {},
                           .decision = {},
                           .diagnostic = {}};
}

void component_protocol_declares_non_convexity_and_returns_general_clauses() {
  SymbolTable symbols;
  TermManager terms(symbols);
  const auto int_sort = symbols.int_sort();
  const auto x_symbol = symbols.declare_constant("x", int_sort);
  const auto y_symbol = symbols.declare_constant("y", int_sort);
  const auto x = terms.mk_constant(x_symbol);
  const auto y = terms.mk_constant(y_symbol);

  MockTheorySolver lia(TheoryProperties{.id = "lia",
                                        .shared_sorts = {int_sort},
                                        .stably_infinite = StableInfiniteness::Yes,
                                        .convexity = Convexity::NonConvex,
                                        .produces_model_projection = true});
  lia.next_result = TheoryCheckResult{.status = TheoryCheckStatus::LearnedClause,
                                      .clause = {primo::sat::Lit{3}, primo::sat::Lit{5}},
                                      .model = {},
                                      .entailed_lit = {},
                                      .reasons = {},
                                      .decision = {},
                                      .diagnostic = {}};
  const std::vector assignment{
      AssignedTheoryLiteral{.atom = terms.mk_le(x, y), .lit = primo::sat::Lit{7}, .value = true}};
  const std::vector arrangement{
      InterfaceRelation{.lhs = x, .rhs = y, .sort = int_sort, .equality_lit = primo::sat::Lit{3}}};

  const TheoryCheckResult result = lia.check(assignment, arrangement);
  assert(lia.properties().id == "lia");
  assert(lia.properties().stably_infinite == StableInfiniteness::Yes);
  assert(lia.properties().convexity == Convexity::NonConvex);
  assert(lia.assigned_count == 1);
  assert(lia.arrangement_count == 1);
  assert(result.status == TheoryCheckStatus::LearnedClause);
  assert(result.clause.size() == 2);
}

void incremental_component_protocol_is_backend_neutral_and_backtrackable() {
  const TheoryProperties properties{.id = "mock-incremental",
                                    .shared_sorts = {},
                                    .stably_infinite = StableInfiniteness::Yes,
                                    .convexity = Convexity::NonConvex,
                                    .produces_model_projection = true};
  MockIncrementalTheorySolver solver(properties);
  const std::vector level_zero{AssignedTheoryLiteral{
      .atom = primo::core::TermId{3}, .lit = primo::sat::Lit{5}, .value = true}};
  const std::vector level_one{AssignedTheoryLiteral{
      .atom = primo::core::TermId{7}, .lit = primo::sat::Lit{11}, .value = false}};

  solver.assign(level_zero);
  solver.push_level();
  solver.assign(level_one);
  assert(solver.check(TheoryCheckEffort::Partial).status == TheoryCheckStatus::Consistent);
  assert(solver.checked_sizes.back() == 2);
  solver.pop_to_level(0);
  assert(solver.check(TheoryCheckEffort::FullModel).status == TheoryCheckStatus::Consistent);
  assert(solver.checked_sizes.back() == 1);
  assert(solver.last_effort == TheoryCheckEffort::FullModel);
  solver.reset();
  assert(solver.trail_.empty());
}

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
  assert(summary.equality_registry.size() == 2);
  assert(summary.missing_equalities == 0);
  assert(summary.visible_equalities.size() == 2);
  assert(summary.visible_equalities[0].lhs == a);
  assert(summary.visible_equalities[0].rhs == b);
  assert(summary.visible_equalities[1].lhs == b);
  assert(summary.visible_equalities[1].rhs == c);
  assert(summary.equality_registry[0].lhs == a);
  assert(summary.equality_registry[0].rhs == b);
  assert(summary.equality_registry[0].status == InterfaceEqualityStatus::Visible);
  assert(summary.equality_registry[1].lhs == b);
  assert(summary.equality_registry[1].rhs == c);
  assert(summary.equality_registry[1].status == InterfaceEqualityStatus::Visible);
}

void ignores_boolean_and_internal_constants() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto aux_symbol = symbols.declare_internal_constant("$primo_aux_0", sort_a);
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
  assert(summary.equality_registry.empty());
  assert(summary.missing_equalities == 0);
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
  assert(summary.equality_registry.size() == 1);
  assert(summary.equality_registry[0].status == InterfaceEqualityStatus::Visible);
}

void ignores_trivial_self_equalities_as_interface_pairs() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto a_symbol = symbols.declare_constant("a", sort_a);

  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);

  const auto summary = analyze_nelson_oppen_interface(terms, {terms.mk_eq(a, a)});

  assert(summary.terms.empty());
  assert(summary.candidate_pairs == 0);
  assert(summary.equality_registry.empty());
  assert(summary.visible_equalities.empty());
  assert(summary.missing_equalities == 0);
}

void registry_supports_mock_theory_dispatch_without_creating_atoms() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto b_symbol = symbols.declare_constant("b", sort_a);
  const auto c_symbol = symbols.declare_constant("c", sort_a);

  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  const auto c = terms.mk_constant(c_symbol);
  const auto eq_ab = terms.mk_eq(a, b);

  const auto summary =
      analyze_nelson_oppen_interface(terms, {eq_ab, terms.mk_not(terms.mk_eq(a, c))});
  MockTheoryDispatch mock;
  mock.inspect(summary);

  assert(summary.candidate_pairs == 3);
  assert(summary.equality_registry.size() == 2);
  assert(mock.visible_pairs == 2);
  assert(mock.missing_pairs == 0);
  const std::vector expected_visible_atoms{eq_ab, terms.mk_eq(a, c)};
  assert(mock.visible_atoms == expected_visible_atoms);
}

void manager_tracks_canonical_interface_equality_values() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto b_symbol = symbols.declare_constant("b", sort_a);
  const auto c_symbol = symbols.declare_constant("c", sort_a);

  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  const auto c = terms.mk_constant(c_symbol);
  const auto eq_ab = terms.mk_eq(a, b);

  TheoryCombinationManager manager(terms);
  manager.rebuild({eq_ab, terms.mk_not(terms.mk_eq(a, c))});

  const auto visible = manager.interface_equality(b, a);
  assert(visible.has_value());
  assert(visible->atom == eq_ab);
  assert(visible->lhs == a);
  assert(visible->rhs == b);
  assert(visible->status == InterfaceEqualityStatus::Visible);
  assert(visible->value == InterfaceEqualityValue::Unknown);
  assert(!manager.interface_equality_lit(a, b).has_value());
  assert(manager.register_interface_equality_atom(eq_ab, primo::sat::Lit{17}));
  assert(manager.interface_equality_lit(a, b).has_value());
  assert(manager.interface_equality_lit(b, a)->value == 17);
  assert(manager.interface_equality_lit(eq_ab)->value == 17);

  assert(manager.set_interface_equality_value(b, a, InterfaceEqualityValue::Equal));
  assert(manager.interface_equality_value(a, b) == InterfaceEqualityValue::Equal);
  assert(manager.interface_equality_value(b, a) == InterfaceEqualityValue::Equal);

  assert(manager.ensure_interface_pair(b, c).has_value());
  assert(manager.set_interface_equality_value(b, c, InterfaceEqualityValue::Disequal));
  const auto missing = manager.interface_equality(c, b);
  assert(missing.has_value());
  assert(missing->atom == primo::core::kInvalidTermId);
  assert(missing->status == InterfaceEqualityStatus::Missing);
  assert(missing->value == InterfaceEqualityValue::Disequal);
  assert(manager.register_interface_equality_atom(terms.mk_eq(b, c), primo::sat::Lit{19}));
  assert(manager.interface_equality_lit(c, b)->value == 19);

  manager.clear_interface_equality_values();
  assert(manager.interface_equality_value(a, b) == InterfaceEqualityValue::Unknown);
  assert(manager.interface_equality_value(b, c) == InterfaceEqualityValue::Unknown);
}

void manager_rejects_unregistered_or_cross_sort_equality_values() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto sort_b = symbols.declare_sort("B");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto b_symbol = symbols.declare_constant("b", sort_a);
  const auto d_symbol = symbols.declare_constant("d", sort_b);
  const auto q_symbol = symbols.declare_predicate("q", {sort_b});

  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  const auto d = terms.mk_constant(d_symbol);
  const auto q_d = terms.mk_app(q_symbol, {d});

  TheoryCombinationManager manager(terms);
  manager.rebuild({terms.mk_eq(a, b), q_d});

  assert(manager.set_interface_equality_value(a, b, InterfaceEqualityValue::Equal));
  assert(!manager.set_interface_equality_value(a, d, InterfaceEqualityValue::Equal));
  assert(!manager.interface_equality(a, d).has_value());
  assert(manager.interface_equality_value(a, d) == InterfaceEqualityValue::Unknown);
}

void manager_rebuild_replaces_previous_arrangement() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto b_symbol = symbols.declare_constant("b", sort_a);

  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);

  TheoryCombinationManager manager(terms);
  manager.rebuild({terms.mk_eq(a, b)});
  assert(manager.set_interface_equality_value(a, b, InterfaceEqualityValue::Equal));
  assert(manager.interface_equality_value(a, b) == InterfaceEqualityValue::Equal);

  manager.rebuild({a});
  assert(manager.summary().equality_registry.empty());
  assert(!manager.interface_equality(a, b).has_value());
  assert(manager.interface_equality_value(a, b) == InterfaceEqualityValue::Unknown);
}

void manager_rolls_back_interface_equality_values_by_decision_level() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto b_symbol = symbols.declare_constant("b", sort_a);
  const auto c_symbol = symbols.declare_constant("c", sort_a);

  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  const auto c = terms.mk_constant(c_symbol);

  TheoryCombinationManager manager(terms);
  manager.rebuild_from_interface_terms({a, b, c});
  assert(manager.ensure_interface_pair(a, b).has_value());
  assert(manager.ensure_interface_pair(b, c).has_value());
  assert(manager.ensure_interface_pair(a, c).has_value());

  assert(manager.set_interface_equality_value(a, b, InterfaceEqualityValue::Equal));
  manager.push_level();
  assert(manager.set_interface_equality_value(a, b, InterfaceEqualityValue::Disequal));
  assert(manager.set_interface_equality_value(b, c, InterfaceEqualityValue::Equal));
  manager.push_level();
  assert(manager.set_interface_equality_value(a, c, InterfaceEqualityValue::Disequal));

  manager.pop_to_level(1);
  assert(manager.interface_equality_value(a, b) == InterfaceEqualityValue::Disequal);
  assert(manager.interface_equality_value(b, c) == InterfaceEqualityValue::Equal);
  assert(manager.interface_equality_value(a, c) == InterfaceEqualityValue::Unknown);

  manager.pop_to_level(0);
  assert(manager.interface_equality_value(a, b) == InterfaceEqualityValue::Equal);
  assert(manager.interface_equality_value(b, c) == InterfaceEqualityValue::Unknown);
  assert(manager.interface_equality_value(a, c) == InterfaceEqualityValue::Unknown);
}

void manager_tracks_interface_equality_assignment_snapshots() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto b_symbol = symbols.declare_constant("b", sort_a);
  const auto c_symbol = symbols.declare_constant("c", sort_a);

  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  const auto c = terms.mk_constant(c_symbol);
  const auto eq_ab = terms.mk_eq(a, b);
  const auto eq_bc = terms.mk_eq(b, c);

  TheoryCombinationManager manager(terms);
  manager.rebuild_from_interface_terms({a, b, c});
  assert(manager.ensure_interface_pair(a, b).has_value());
  assert(manager.ensure_interface_pair(b, c).has_value());
  assert(manager.register_interface_equality_atom(eq_ab, primo::sat::Lit{17}));
  assert(manager.register_interface_equality_atom(eq_bc, primo::sat::Lit{19}));

  manager.refresh_interface_equality_assignments([](primo::sat::Lit lit) -> std::optional<bool> {
    if (lit.value == 17) {
      return true;
    }
    if (lit.value == 19) {
      return false;
    }
    return std::nullopt;
  });

  assert(manager.interface_equality_assignment(a, b) == InterfaceEqualityAssignment::True);
  assert(manager.interface_equality_assignment(b, c) == InterfaceEqualityAssignment::False);
  assert(manager.interface_equality_assignment(a, c) == InterfaceEqualityAssignment::Unknown);
  assert(manager.interface_equalities_with_assignment(InterfaceEqualityAssignment::True).size() ==
         1);
  assert(manager.interface_equalities_with_assignment(InterfaceEqualityAssignment::False).size() ==
         1);
  assert(manager.registered_interface_equality_atom_count() == 2);

  manager.clear_interface_equality_assignments();
  assert(manager.interface_equality_assignment(a, b) == InterfaceEqualityAssignment::Unknown);
  assert(manager.interface_equality_lit(a, b)->value == 17);
}

void manager_rolls_back_interface_equality_assignments_by_decision_level() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto b_symbol = symbols.declare_constant("b", sort_a);
  const auto c_symbol = symbols.declare_constant("c", sort_a);

  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  const auto c = terms.mk_constant(c_symbol);

  TheoryCombinationManager manager(terms);
  manager.rebuild_from_interface_terms({a, b, c});
  assert(manager.ensure_interface_pair(a, b).has_value());
  assert(manager.ensure_interface_pair(b, c).has_value());
  assert(manager.ensure_interface_pair(a, c).has_value());

  assert(manager.set_interface_equality_assignment(a, b, InterfaceEqualityAssignment::True));
  manager.push_level();
  assert(manager.set_interface_equality_assignment(a, b, InterfaceEqualityAssignment::False));
  assert(manager.set_interface_equality_assignment(b, c, InterfaceEqualityAssignment::True));
  manager.push_level();
  assert(manager.set_interface_equality_assignment(a, c, InterfaceEqualityAssignment::False));

  manager.pop_to_level(1);
  assert(manager.interface_equality_assignment(a, b) == InterfaceEqualityAssignment::False);
  assert(manager.interface_equality_assignment(b, c) == InterfaceEqualityAssignment::True);
  assert(manager.interface_equality_assignment(a, c) == InterfaceEqualityAssignment::Unknown);

  manager.pop_to_level(0);
  assert(manager.interface_equality_assignment(a, b) == InterfaceEqualityAssignment::True);
  assert(manager.interface_equality_assignment(b, c) == InterfaceEqualityAssignment::Unknown);
  assert(manager.interface_equality_assignment(a, c) == InterfaceEqualityAssignment::Unknown);
}

void manager_tracks_per_round_interface_equality_classifications() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto b_symbol = symbols.declare_constant("b", sort_a);
  const auto c_symbol = symbols.declare_constant("c", sort_a);

  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  const auto c = terms.mk_constant(c_symbol);
  const auto eq_ab = terms.mk_eq(a, b);

  TheoryCombinationManager manager(terms);
  manager.rebuild_from_interface_terms({a, b, c});
  assert(manager.ensure_interface_pair(a, b).has_value());

  assert(manager.mark_interface_equality_classification(
      eq_ab, InterfaceEqualityClassification::LraImplied));
  assert(manager.mark_interface_equality_classification(
      b, a, InterfaceEqualityClassification::SatForced));
  assert(manager.has_interface_equality_classification(
      a, b, InterfaceEqualityClassification::LraImplied));
  assert(manager.has_interface_equality_classification(a, b,
                                                       InterfaceEqualityClassification::SatForced));
  assert(!manager.mark_interface_equality_classification(
      terms.mk_eq(a, a), InterfaceEqualityClassification::EufImplied));

  manager.clear_interface_equality_classifications();
  assert(!manager.has_interface_equality_classification(
      a, b, InterfaceEqualityClassification::LraImplied));
  assert(manager.interface_equality(a, b).has_value());
}

void manager_owns_round_scoped_shared_model_state() {
  SymbolTable symbols;
  const auto real_sort = symbols.real_sort();
  const auto a_symbol = symbols.declare_constant("a", real_sort);
  const auto b_symbol = symbols.declare_constant("b", real_sort);
  const auto c_symbol = symbols.declare_constant("c", real_sort);

  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  const auto c = terms.mk_constant(c_symbol);

  TheoryCombinationManager manager(terms);
  manager.rebuild_from_interface_terms({a, b, c});
  assert(!manager.shared_model().valid);

  manager.begin_shared_model_round();
  const std::uint64_t generation = manager.shared_model().generation;
  manager.begin_shared_model_iteration({{c, a, b}, {a}}, {a, b, c});
  assert(manager.shared_model().valid);
  assert(manager.shared_model().reconciliation_iteration == 1);
  assert(manager.shared_model().lra_value_classes.size() == 1);
  const std::vector<SharedModelEquality> expected_spanning{SharedModelEquality{a, b},
                                                           SharedModelEquality{a, c}};
  assert(manager.shared_model().lra_model_spanning_equalities == expected_spanning);
  assert(manager.lra_model_terms_mapped(a, c));
  assert(manager.lra_model_agrees_equal(a, c));

  assert(manager.enqueue_explained_shared_equality(
      c, a, {primo::sat::Lit{-3}, primo::sat::Lit{-5}, primo::sat::Lit{-3}},
      SharedEqualitySource::LraEntailed));
  assert(!manager.enqueue_explained_shared_equality(a, c, {primo::sat::Lit{-4}},
                                                    SharedEqualitySource::EufImplied));
  const auto explained = manager.drain_explained_shared_equalities();
  assert(explained.size() == 1);
  assert(explained.front().lhs == a);
  assert(explained.front().rhs == c);
  assert(explained.front().reasons.size() == 1);
  assert(explained.front().reasons.front().value == -4);
  assert(explained.front().source == SharedEqualitySource::EufImplied);
  assert(manager.drain_explained_shared_equalities().empty());

  assert(manager.record_internal_model_equality(c, a));
  assert(!manager.record_internal_model_equality(a, c));
  assert(manager.has_interface_equality_classification(
      a, c, InterfaceEqualityClassification::ModelInternal));
  const auto separation = terms.mk_lt(b, c);
  assert(manager.record_lra_separation_atom(separation));
  assert(!manager.record_lra_separation_atom(separation));

  manager.begin_shared_model_iteration({{a, c}}, {a, b, c});
  assert(manager.shared_model().reconciliation_iteration == 2);
  assert(manager.shared_model().internal_equalities.empty());
  assert(!manager.has_interface_equality_classification(
      a, c, InterfaceEqualityClassification::ModelInternal));
  assert(manager.shared_model().lra_separation_atoms.size() == 1);
  assert(manager.lra_model_terms_mapped(a, b));
  assert(!manager.lra_model_agrees_equal(a, b));

  manager.invalidate_shared_model();
  assert(!manager.shared_model().valid);
  manager.begin_shared_model_round();
  assert(manager.shared_model().generation == generation + 1);
  manager.rebuild_from_interface_terms({a, b});
  assert(!manager.shared_model().valid);
}

void manager_rebuilds_registry_from_explicit_interface_terms() {
  SymbolTable symbols;
  const auto real_sort = symbols.real_sort();
  const auto flag_symbol = symbols.declare_constant("flag", symbols.bool_sort());
  const auto x_symbol = symbols.declare_constant("x", real_sort);
  const auto y_symbol = symbols.declare_constant("y", real_sort);
  const auto z_symbol = symbols.declare_constant("z", real_sort);

  TermManager terms(symbols);
  const auto flag = terms.mk_constant(flag_symbol);
  const auto x = terms.mk_constant(x_symbol);
  const auto y = terms.mk_constant(y_symbol);
  const auto z = terms.mk_constant(z_symbol);
  const auto eq_xy = terms.mk_eq(x, y);

  TheoryCombinationManager manager(terms);
  manager.rebuild_from_interface_terms({x, y, y, flag, z});

  assert(manager.summary().terms.size() == 3);
  assert(manager.summary().sort_count == 1);
  assert(manager.summary().candidate_pairs == 3);
  assert(manager.summary().equality_registry.empty());
  assert(manager.summary().missing_equalities == 0);
  assert(!manager.interface_equality(y, x).has_value());
  assert(manager.ensure_interface_pair(y, x).has_value());
  assert(!manager.interface_equality_lit(x, y).has_value());
  assert(!manager.register_interface_equality_atom(eq_xy, primo::sat::Lit{0}));
  assert(manager.register_interface_equality_atom(eq_xy, primo::sat::Lit{23}));
  assert(manager.interface_equality_lit(y, x)->value == 23);
  assert(manager.summary().missing_equalities == 0);
}

void manager_owns_lazy_shared_equality_decisions() {
  SymbolTable symbols;
  const auto real_sort = symbols.real_sort();
  const auto x_symbol = symbols.declare_constant("x", real_sort);
  const auto y_symbol = symbols.declare_constant("y", real_sort);
  TermManager terms(symbols);
  const auto x = terms.mk_constant(x_symbol);
  const auto y = terms.mk_constant(y_symbol);

  TheoryCombinationManager manager(terms);
  manager.rebuild_from_interface_terms({x, y});
  auto& decisions = manager.shared_equality_decisions();
  decisions.tried_vars.insert(7);
  decisions.pending.push_back(primo::sat::Lit{7});
  decisions.pending.push_back(primo::sat::Lit{9});
  decisions.active.push_back(primo::sat::Lit{5});

  manager.begin_shared_model_round();
  assert(manager.shared_equality_decisions().tried_vars.contains(7));
  assert(manager.shared_equality_decisions().pending.size() == 2);
  assert(manager.shared_equality_decisions().pending.front().value == 7);
  assert(manager.shared_equality_decisions().pending.back().value == 9);
  assert(manager.shared_equality_decisions().active.size() == 1);

  manager.rebuild_from_interface_terms({x, y});
  assert(manager.shared_equality_decisions().tried_vars.empty());
  assert(manager.shared_equality_decisions().pending.empty());
  assert(manager.shared_equality_decisions().active.empty());
}

void reports_real_terms_inside_euf_atoms_as_purification_sites() {
  SymbolTable symbols;
  const auto real_sort = symbols.real_sort();
  const auto x_symbol = symbols.declare_constant("x", real_sort);
  const auto y_symbol = symbols.declare_constant("y", real_sort);
  const auto p_symbol = symbols.declare_predicate("p", {real_sort});

  TermManager terms(symbols);
  const auto x = terms.mk_constant(x_symbol);
  const auto y = terms.mk_constant(y_symbol);
  const auto p_x = terms.mk_app(p_symbol, {x});
  const auto p_y = terms.mk_app(p_symbol, {y});
  const auto formula = terms.mk_and({p_x, terms.mk_not(p_y)});

  const auto summary = analyze_nelson_oppen_interface(terms, {formula});

  assert(summary.euf_real_terms == 2);
  assert(summary.arithmetic_uninterpreted_applications == 0);
  assert(summary.purification_sites.size() == 2);
  assert(summary.purification_sites[0].kind == PurificationSiteKind::RealTermInEufAtom);
  assert(summary.purification_sites[0].term == x);
  assert(summary.purification_sites[0].atom == p_x);
  assert(summary.purification_sites[1].kind == PurificationSiteKind::RealTermInEufAtom);
  assert(summary.purification_sites[1].term == y);
  assert(summary.purification_sites[1].atom == p_y);
}

void reports_uninterpreted_applications_inside_arithmetic_atoms() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto real_sort = symbols.real_sort();
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto f_symbol = symbols.declare_function("f", {sort_a}, real_sort);

  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto f_a = terms.mk_app(f_symbol, {a});
  const auto one = terms.mk_rational(primo::util::Rational(1));
  const auto atom = terms.mk_lt(f_a, one);

  const auto summary = analyze_nelson_oppen_interface(terms, {atom});

  assert(summary.euf_real_terms == 0);
  assert(summary.arithmetic_uninterpreted_applications == 1);
  assert(summary.purification_sites.size() == 1);
  assert(summary.purification_sites[0].kind ==
         PurificationSiteKind::UninterpretedApplicationInArithmeticAtom);
  assert(summary.purification_sites[0].term == f_a);
  assert(summary.purification_sites[0].atom == atom);
}

void model_coordinator_accepts_agreeing_current_snapshots() {
  SymbolTable symbols;
  const auto sort = symbols.declare_sort("S");
  const auto a_symbol = symbols.declare_constant("a", sort);
  const auto b_symbol = symbols.declare_constant("b", sort);
  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);

  MockTheorySolver left(TheoryProperties{.id = "left",
                                         .shared_sorts = {sort},
                                         .stably_infinite = StableInfiniteness::Yes,
                                         .convexity = Convexity::Convex,
                                         .produces_model_projection = true});
  MockTheorySolver right(TheoryProperties{.id = "right",
                                          .shared_sorts = {sort},
                                          .stably_infinite = StableInfiniteness::Yes,
                                          .convexity = Convexity::Convex,
                                          .produces_model_projection = true});
  left.next_result = consistent_with(projection("left", 4, 1, sort, {{a, b}}));
  right.next_result = consistent_with(projection("right", 4, 7, sort, {{b, a}}));
  const std::vector<ModelBasedComponentRound> rounds{{.solver = &left, .assignment = {}},
                                                     {.solver = &right, .assignment = {}}};
  const std::vector interfaces{SharedInterfaceTerms{.sort = sort, .terms = {a, b}}};
  ModelBasedCombinationCoordinator coordinator;

  const auto result = coordinator.reconcile(4, rounds, {}, interfaces, [](const InterfacePair&) {
    return std::optional{primo::sat::Lit{9}};
  });

  assert(result.status == ModelBasedCombinationStatus::Accept);
  assert(coordinator.statistics().component_checks == 2);
  assert(coordinator.statistics().snapshots == 2);
}

void model_coordinator_rejects_stale_snapshots() {
  SymbolTable symbols;
  const auto sort = symbols.declare_sort("S");
  const auto a_symbol = symbols.declare_constant("a", sort);
  const auto b_symbol = symbols.declare_constant("b", sort);
  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  MockTheorySolver solver(TheoryProperties{.id = "component",
                                           .shared_sorts = {sort},
                                           .stably_infinite = StableInfiniteness::Yes,
                                           .convexity = Convexity::Convex,
                                           .produces_model_projection = true});
  solver.next_result = consistent_with(projection("component", 2, 1, sort, {{a}, {b}}));
  const std::vector<ModelBasedComponentRound> rounds{{.solver = &solver, .assignment = {}}};
  const std::vector interfaces{SharedInterfaceTerms{.sort = sort, .terms = {a, b}}};
  ModelBasedCombinationCoordinator coordinator;

  const auto result = coordinator.reconcile(3, rounds, {}, interfaces, {});

  assert(result.status == ModelBasedCombinationStatus::Unknown);
  assert(result.diagnostic == "stale theory model snapshot");
  assert(coordinator.statistics().stale_snapshots == 1);
}

void model_coordinator_rejects_incomplete_projections_and_preserves_exceptions() {
  SymbolTable symbols;
  const auto sort = symbols.declare_sort("S");
  const auto a_symbol = symbols.declare_constant("a", sort);
  const auto b_symbol = symbols.declare_constant("b", sort);
  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  MockTheorySolver solver(TheoryProperties{.id = "component",
                                           .shared_sorts = {sort},
                                           .stably_infinite = StableInfiniteness::Yes,
                                           .convexity = Convexity::Convex,
                                           .produces_model_projection = true});
  solver.next_result = consistent_with(projection("component", 3, 1, sort, {{a}}));
  const std::vector<ModelBasedComponentRound> rounds{{.solver = &solver, .assignment = {}}};
  const std::vector interfaces{SharedInterfaceTerms{.sort = sort, .terms = {a, b}}};
  ModelBasedCombinationCoordinator coordinator;

  auto result = coordinator.reconcile(3, rounds, {}, interfaces, {});
  assert(result.status == ModelBasedCombinationStatus::Unknown);
  assert(result.diagnostic == "incomplete or malformed theory model projection");

  solver.throw_on_check = true;
  bool escaped = false;
  try {
    static_cast<void>(coordinator.reconcile(4, rounds, {}, interfaces, {}));
  } catch (const std::runtime_error& error) {
    escaped = std::string(error.what()) == "scripted component failure";
  }
  assert(escaped);
}

void model_coordinator_mutates_a_projection_to_agreement() {
  SymbolTable symbols;
  const auto sort = symbols.declare_sort("S");
  const auto a_symbol = symbols.declare_constant("a", sort);
  const auto b_symbol = symbols.declare_constant("b", sort);
  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  MockTheorySolver separated(TheoryProperties{.id = "separated",
                                              .shared_sorts = {sort},
                                              .stably_infinite = StableInfiniteness::Yes,
                                              .convexity = Convexity::Convex,
                                              .produces_model_projection = true});
  MockTheorySolver merged(TheoryProperties{.id = "merged",
                                           .shared_sorts = {sort},
                                           .stably_infinite = StableInfiniteness::Yes,
                                           .convexity = Convexity::Convex,
                                           .produces_model_projection = true});
  separated.next_result = consistent_with(projection("separated", 8, 1, sort, {{a}, {b}}));
  merged.next_result = consistent_with(projection("merged", 8, 3, sort, {{a, b}}));
  merged.next_mutation = TheoryMutationResult{.status = TheoryMutationStatus::Mutated,
                                              .model = projection("merged", 8, 4, sort, {{a}, {b}}),
                                              .candidate_decisions = {},
                                              .diagnostic = {}};
  const std::vector<ModelBasedComponentRound> rounds{{.solver = &separated, .assignment = {}},
                                                     {.solver = &merged, .assignment = {}}};
  const std::vector interfaces{SharedInterfaceTerms{.sort = sort, .terms = {a, b}}};
  ModelBasedCombinationCoordinator coordinator;

  const auto result = coordinator.reconcile(8, rounds, {}, interfaces, {});

  assert(result.status == ModelBasedCombinationStatus::Accept);
  assert(merged.mutation_count == 1);
  assert(merged.last_mutation_source.revision == 3);
  assert(coordinator.statistics().mutations == 1);
}

void model_coordinator_accepts_a_compatible_model_variant() {
  SymbolTable symbols;
  const auto sort = symbols.declare_sort("S");
  const auto a_symbol = symbols.declare_constant("a", sort);
  const auto b_symbol = symbols.declare_constant("b", sort);
  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  MockTheorySolver merged(TheoryProperties{.id = "merged",
                                           .shared_sorts = {sort},
                                           .stably_infinite = StableInfiniteness::Yes,
                                           .convexity = Convexity::Convex,
                                           .produces_model_projection = true});
  MockTheorySolver separated(TheoryProperties{.id = "separated",
                                              .shared_sorts = {sort},
                                              .stably_infinite = StableInfiniteness::Yes,
                                              .convexity = Convexity::Convex,
                                              .produces_model_projection = true});
  merged.next_result = consistent_with(projection("merged", 10, 1, sort, {{a, b}}));
  separated.next_result = consistent_with(projection("separated", 10, 1, sort, {{a}, {b}}));
  separated.next_mutation =
      TheoryMutationResult{.status = TheoryMutationStatus::Mutated,
                           .model = projection("separated", 10, 2, sort, {{a, b}}),
                           .candidate_decisions = {},
                           .diagnostic = {}};
  const std::vector<ModelBasedComponentRound> rounds{{.solver = &merged, .assignment = {}},
                                                     {.solver = &separated, .assignment = {}}};
  const std::vector interfaces{SharedInterfaceTerms{.sort = sort, .terms = {a, b}}};
  ModelBasedCombinationCoordinator coordinator;

  const auto result = coordinator.reconcile(10, rounds, {}, interfaces, {});

  assert(result.status == ModelBasedCombinationStatus::Accept);
  assert(separated.mutation_count == 1);
}

// A mutation is validated only against the relations the coordinator asked for,
// so it may freely merge or split shared pairs that no projection made visible
// before the mutation. The post-mutation agreement check must therefore be
// derived from the mutated projections, not from the pre-mutation comparison
// set.
void model_coordinator_rejects_a_mutation_that_splits_an_unlisted_pair() {
  SymbolTable symbols;
  const auto sort = symbols.declare_sort("S");
  const auto x_symbol = symbols.declare_constant("x", sort);
  const auto y_symbol = symbols.declare_constant("y", sort);
  const auto z_symbol = symbols.declare_constant("z", sort);
  const auto w_symbol = symbols.declare_constant("w", sort);
  TermManager terms(symbols);
  const auto x = terms.mk_constant(x_symbol);
  const auto y = terms.mk_constant(y_symbol);
  const auto z = terms.mk_constant(z_symbol);
  const auto w = terms.mk_constant(w_symbol);
  MockTheorySolver reference(TheoryProperties{.id = "reference",
                                              .shared_sorts = {sort},
                                              .stably_infinite = StableInfiniteness::Yes,
                                              .convexity = Convexity::Convex,
                                              .produces_model_projection = true});
  MockTheorySolver variant(TheoryProperties{.id = "variant",
                                            .shared_sorts = {sort},
                                            .stably_infinite = StableInfiniteness::Yes,
                                            .convexity = Convexity::Convex,
                                            .produces_model_projection = true});
  // Only (x, y) is a comparison pair before the mutation: it is the single
  // non-singleton class across both projections.
  reference.next_result = consistent_with(projection("reference", 12, 1, sort, {{x, y}, {z}, {w}}));
  variant.next_result = consistent_with(projection("variant", 12, 1, sort, {{x}, {y}, {z}, {w}}));
  // The mutation reaches the requested (x, y) merge but also merges (z, w),
  // which the reference keeps apart.
  variant.next_mutation =
      TheoryMutationResult{.status = TheoryMutationStatus::Mutated,
                           .model = projection("variant", 12, 2, sort, {{x, y}, {z, w}}),
                           .candidate_decisions = {},
                           .diagnostic = {}};
  const std::vector<ModelBasedComponentRound> rounds{{.solver = &reference, .assignment = {}},
                                                     {.solver = &variant, .assignment = {}}};
  const std::vector interfaces{SharedInterfaceTerms{.sort = sort, .terms = {x, y, z, w}}};
  ModelBasedCombinationCoordinator coordinator;

  const auto result =
      coordinator.reconcile(12, rounds, {}, interfaces, [z, w](const InterfacePair& pair) {
        const bool is_zw = (pair.lhs == z && pair.rhs == w) || (pair.lhs == w && pair.rhs == z);
        return std::optional{primo::sat::Lit{is_zw ? 47 : 11}};
      });

  assert(variant.mutation_count == 1);
  assert(result.status == ModelBasedCombinationStatus::DecideEquality);
  assert(result.decision_lit.value == 47);
  assert(coordinator.statistics().disagreements == 1);
}

void model_coordinator_accepts_a_mutation_that_reaches_full_agreement() {
  SymbolTable symbols;
  const auto sort = symbols.declare_sort("S");
  const auto x_symbol = symbols.declare_constant("x", sort);
  const auto y_symbol = symbols.declare_constant("y", sort);
  const auto z_symbol = symbols.declare_constant("z", sort);
  const auto w_symbol = symbols.declare_constant("w", sort);
  TermManager terms(symbols);
  const auto x = terms.mk_constant(x_symbol);
  const auto y = terms.mk_constant(y_symbol);
  const auto z = terms.mk_constant(z_symbol);
  const auto w = terms.mk_constant(w_symbol);
  MockTheorySolver reference(TheoryProperties{.id = "reference",
                                              .shared_sorts = {sort},
                                              .stably_infinite = StableInfiniteness::Yes,
                                              .convexity = Convexity::Convex,
                                              .produces_model_projection = true});
  MockTheorySolver variant(TheoryProperties{.id = "variant",
                                            .shared_sorts = {sort},
                                            .stably_infinite = StableInfiniteness::Yes,
                                            .convexity = Convexity::Convex,
                                            .produces_model_projection = true});
  reference.next_result = consistent_with(projection("reference", 13, 1, sort, {{x, y}, {z}, {w}}));
  variant.next_result = consistent_with(projection("variant", 13, 1, sort, {{x}, {y}, {z}, {w}}));
  variant.next_mutation =
      TheoryMutationResult{.status = TheoryMutationStatus::Mutated,
                           .model = projection("variant", 13, 2, sort, {{x, y}, {z}, {w}}),
                           .candidate_decisions = {},
                           .diagnostic = {}};
  const std::vector<ModelBasedComponentRound> rounds{{.solver = &reference, .assignment = {}},
                                                     {.solver = &variant, .assignment = {}}};
  const std::vector interfaces{SharedInterfaceTerms{.sort = sort, .terms = {x, y, z, w}}};
  ModelBasedCombinationCoordinator coordinator;

  const auto result = coordinator.reconcile(13, rounds, {}, interfaces, {});

  assert(variant.mutation_count == 1);
  assert(result.status == ModelBasedCombinationStatus::Accept);
  assert(coordinator.statistics().disagreements == 0);
}

void model_coordinator_turns_fixed_point_disagreement_into_a_choice() {
  SymbolTable symbols;
  const auto sort = symbols.declare_sort("S");
  const auto a_symbol = symbols.declare_constant("a", sort);
  const auto b_symbol = symbols.declare_constant("b", sort);
  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  MockTheorySolver separated(TheoryProperties{.id = "separated",
                                              .shared_sorts = {sort},
                                              .stably_infinite = StableInfiniteness::Yes,
                                              .convexity = Convexity::Convex,
                                              .produces_model_projection = true});
  MockTheorySolver merged(TheoryProperties{.id = "merged",
                                           .shared_sorts = {sort},
                                           .stably_infinite = StableInfiniteness::Yes,
                                           .convexity = Convexity::Convex,
                                           .produces_model_projection = true});
  separated.next_result = consistent_with(projection("separated", 5, 1, sort, {{a}, {b}}));
  merged.next_result = consistent_with(projection("merged", 5, 1, sort, {{a, b}}));
  const std::vector<ModelBasedComponentRound> rounds{{.solver = &separated, .assignment = {}},
                                                     {.solver = &merged, .assignment = {}}};
  const std::vector interfaces{SharedInterfaceTerms{.sort = sort, .terms = {a, b}}};
  ModelBasedCombinationCoordinator coordinator;

  const auto result = coordinator.reconcile(5, rounds, {}, interfaces, [](const InterfacePair&) {
    return std::optional{primo::sat::Lit{31}};
  });

  assert(result.status == ModelBasedCombinationStatus::DecideEquality);
  assert(result.decision_lit.value == 31);
  assert(coordinator.statistics().fixed_points == 1);
  assert(coordinator.statistics().disagreements == 1);
}

void model_coordinator_rejects_unchanged_mutations() {
  SymbolTable symbols;
  const auto sort = symbols.declare_sort("S");
  const auto a_symbol = symbols.declare_constant("a", sort);
  const auto b_symbol = symbols.declare_constant("b", sort);
  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  MockTheorySolver merged(TheoryProperties{.id = "merged",
                                           .shared_sorts = {sort},
                                           .stably_infinite = StableInfiniteness::Yes,
                                           .convexity = Convexity::Convex,
                                           .produces_model_projection = true});
  MockTheorySolver separated(TheoryProperties{.id = "separated",
                                              .shared_sorts = {sort},
                                              .stably_infinite = StableInfiniteness::Yes,
                                              .convexity = Convexity::Convex,
                                              .produces_model_projection = true});
  merged.next_result = consistent_with(projection("merged", 6, 1, sort, {{a, b}}));
  separated.next_result = consistent_with(projection("separated", 6, 1, sort, {{a}, {b}}));
  merged.next_mutation = TheoryMutationResult{.status = TheoryMutationStatus::Mutated,
                                              .model = projection("merged", 6, 2, sort, {{a, b}}),
                                              .candidate_decisions = {},
                                              .diagnostic = {}};
  const std::vector<ModelBasedComponentRound> rounds{{.solver = &separated, .assignment = {}},
                                                     {.solver = &merged, .assignment = {}}};
  const std::vector interfaces{SharedInterfaceTerms{.sort = sort, .terms = {a, b}}};
  ModelBasedCombinationCoordinator coordinator;

  const auto result = coordinator.reconcile(6, rounds, {}, interfaces, {});

  assert(result.status == ModelBasedCombinationStatus::Unknown);
  assert(result.diagnostic == "invalid theory model mutation");
  assert(coordinator.statistics().rejected_mutations == 1);
}

// A component that mutates must advance its model revision. That is easy to
// get wrong -- the LIA and LRA projection helpers both stamp a fixed revision
// -- and the failure looks like a generic invalid model, so the diagnostic
// names the actual cause.
void model_coordinator_names_a_mutation_that_does_not_advance_the_revision() {
  SymbolTable symbols;
  const auto sort = symbols.declare_sort("S");
  const auto a_symbol = symbols.declare_constant("a", sort);
  const auto b_symbol = symbols.declare_constant("b", sort);
  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  MockTheorySolver merged(TheoryProperties{.id = "merged",
                                           .shared_sorts = {sort},
                                           .stably_infinite = StableInfiniteness::Yes,
                                           .convexity = Convexity::Convex,
                                           .produces_model_projection = true});
  MockTheorySolver separated(TheoryProperties{.id = "separated",
                                              .shared_sorts = {sort},
                                              .stably_infinite = StableInfiniteness::Yes,
                                              .convexity = Convexity::Convex,
                                              .produces_model_projection = true});
  merged.next_result = consistent_with(projection("merged", 21, 1, sort, {{a, b}}));
  separated.next_result = consistent_with(projection("separated", 21, 1, sort, {{a}, {b}}));
  // A correct partition, but stamped with the revision it started from -- the
  // shape a hard-coded revision in a projection helper produces.
  separated.next_mutation =
      TheoryMutationResult{.status = TheoryMutationStatus::Mutated,
                           .model = projection("separated", 21, 1, sort, {{a, b}}),
                           .candidate_decisions = {},
                           .diagnostic = {}};
  const std::vector<ModelBasedComponentRound> rounds{{.solver = &merged, .assignment = {}},
                                                     {.solver = &separated, .assignment = {}}};
  const std::vector interfaces{SharedInterfaceTerms{.sort = sort, .terms = {a, b}}};
  ModelBasedCombinationCoordinator coordinator;

  const auto result = coordinator.reconcile(21, rounds, {}, interfaces, {});

  assert(result.status == ModelBasedCombinationStatus::Unknown);
  assert(result.diagnostic == "theory model mutation did not advance its revision");
  assert(coordinator.statistics().rejected_mutations == 1);
}

void model_coordinator_prioritizes_care_pair_disagreements() {
  SymbolTable symbols;
  const auto sort = symbols.declare_sort("S");
  const auto a_symbol = symbols.declare_constant("a", sort);
  const auto b_symbol = symbols.declare_constant("b", sort);
  const auto c_symbol = symbols.declare_constant("c", sort);
  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  const auto c = terms.mk_constant(c_symbol);
  MockTheorySolver separated(TheoryProperties{.id = "separated",
                                              .shared_sorts = {sort},
                                              .stably_infinite = StableInfiniteness::Yes,
                                              .convexity = Convexity::Convex,
                                              .produces_model_projection = true});
  MockTheorySolver merged(TheoryProperties{.id = "merged",
                                           .shared_sorts = {sort},
                                           .stably_infinite = StableInfiniteness::Yes,
                                           .convexity = Convexity::Convex,
                                           .produces_model_projection = true,
                                           .supplies_care_pairs = true});
  separated.next_result = consistent_with(projection("separated", 9, 1, sort, {{a}, {b}, {c}}));
  merged.next_result = consistent_with(projection("merged", 9, 1, sort, {{a, b, c}}));
  merged.next_care_pairs = {InterfacePair{.lhs = b, .rhs = c, .sort = sort}};
  const std::vector<ModelBasedComponentRound> rounds{{.solver = &separated, .assignment = {}},
                                                     {.solver = &merged, .assignment = {}}};
  const std::vector interfaces{SharedInterfaceTerms{.sort = sort, .terms = {a, b, c}}};
  ModelBasedCombinationCoordinator coordinator;
  InterfacePair selected;

  const auto result =
      coordinator.reconcile(9, rounds, {}, interfaces, [&selected](const InterfacePair& pair) {
        selected = pair;
        return std::optional{primo::sat::Lit{37}};
      });

  assert(result.status == ModelBasedCombinationStatus::DecideEquality);
  assert(selected.lhs == b);
  assert(selected.rhs == c);
  assert(coordinator.statistics().care_pairs == 1);
}

void model_coordinator_batches_component_model_choices() {
  SymbolTable symbols;
  const auto sort = symbols.declare_sort("S");
  const auto a_symbol = symbols.declare_constant("a", sort);
  const auto b_symbol = symbols.declare_constant("b", sort);
  const auto c_symbol = symbols.declare_constant("c", sort);
  TermManager terms(symbols);
  const auto a = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  const auto c = terms.mk_constant(c_symbol);
  MockTheorySolver merged(TheoryProperties{.id = "merged",
                                           .shared_sorts = {sort},
                                           .stably_infinite = StableInfiniteness::Yes,
                                           .convexity = Convexity::Convex,
                                           .produces_model_projection = true});
  MockTheorySolver separated(TheoryProperties{.id = "separated",
                                              .shared_sorts = {sort},
                                              .stably_infinite = StableInfiniteness::Yes,
                                              .convexity = Convexity::Convex,
                                              .produces_model_projection = true});
  merged.next_result = consistent_with(projection("merged", 11, 1, sort, {{a, b, c}}));
  separated.next_result = consistent_with(projection("separated", 11, 1, sort, {{a}, {b}, {c}}));
  separated.next_mutation.candidate_decisions = {{.pair = {.lhs = a, .rhs = b, .sort = sort},
                                                  .preferred_value = InterfaceRelationValue::Equal,
                                                  .reasons = {}},
                                                 {.pair = {.lhs = a, .rhs = c, .sort = sort},
                                                  .preferred_value = InterfaceRelationValue::Equal,
                                                  .reasons = {}}};
  const std::vector<ModelBasedComponentRound> rounds{{.solver = &merged, .assignment = {}},
                                                     {.solver = &separated, .assignment = {}}};
  const std::vector interfaces{SharedInterfaceTerms{.sort = sort, .terms = {a, b, c}}};
  ModelBasedCombinationCoordinator coordinator;

  const auto result =
      coordinator.reconcile(11, rounds, {}, interfaces, [a](const InterfacePair& pair) {
        return std::optional{primo::sat::Lit{pair.lhs == a && pair.rhs == a + 1 ? 41 : 43}};
      });

  assert(result.status == ModelBasedCombinationStatus::DecideEquality);
  assert(result.decision_lits.size() == 2);
  assert(result.decision_lits[0].value == 41);
  assert(result.decision_lits[1].value == 43);
  assert(coordinator.statistics().model_decisions == 2);
}

void model_coordinator_keeps_propagation_splitting_and_cancellation_typed() {
  MockTheorySolver solver(TheoryProperties{.id = "lia",
                                           .shared_sorts = {},
                                           .stably_infinite = StableInfiniteness::Yes,
                                           .convexity = Convexity::NonConvex,
                                           .produces_model_projection = true});
  const std::vector<ModelBasedComponentRound> rounds{{.solver = &solver, .assignment = {}}};
  ModelBasedCombinationCoordinator coordinator;

  solver.next_result = TheoryCheckResult{.status = TheoryCheckStatus::EntailedLiteral,
                                         .clause = {},
                                         .model = {},
                                         .entailed_lit = primo::sat::Lit{11},
                                         .reasons = {primo::sat::Lit{3}, primo::sat::Lit{-5}},
                                         .decision = {},
                                         .diagnostic = {}};
  auto result = coordinator.reconcile(1, rounds, {}, {}, {});
  assert(result.status == ModelBasedCombinationStatus::LearnClause);
  assert(result.clause.size() == 3);
  assert(result.clause[0].value == -3);
  assert(result.clause[1].value == 5);
  assert(result.clause[2].value == 11);

  solver.next_result =
      TheoryCheckResult{.status = TheoryCheckStatus::FreshDecision,
                        .clause = {},
                        .model = {},
                        .entailed_lit = {},
                        .reasons = {},
                        .decision = {.pair = {.lhs = primo::core::TermId{7},
                                              .rhs = primo::core::TermId{9},
                                              .sort = primo::core::SortId{2}},
                                     .preferred_value = InterfaceRelationValue::Disequal,
                                     .reasons = {primo::sat::Lit{13}}},
                        .diagnostic = {}};
  result = coordinator.reconcile(2, rounds, {}, {}, {});
  assert(result.status == ModelBasedCombinationStatus::FreshDecision);
  assert(result.decision.preferred_value == InterfaceRelationValue::Disequal);

  solver.next_result = TheoryCheckResult{.status = TheoryCheckStatus::LearnedClause,
                                         .clause = {primo::sat::Lit{17}, primo::sat::Lit{19}},
                                         .model = {},
                                         .entailed_lit = {},
                                         .reasons = {},
                                         .decision = {},
                                         .diagnostic = {}};
  result = coordinator.reconcile(3, rounds, {}, {}, {});
  assert(result.status == ModelBasedCombinationStatus::LearnClause);
  assert(result.clause.size() == 2);

  solver.next_result = TheoryCheckResult{.status = TheoryCheckStatus::Cancelled,
                                         .clause = {},
                                         .model = {},
                                         .entailed_lit = {},
                                         .reasons = {},
                                         .decision = {},
                                         .diagnostic = "cancelled"};
  result = coordinator.reconcile(4, rounds, {}, {}, {});
  assert(result.status == ModelBasedCombinationStatus::Cancelled);
}

}  // namespace

int main() {
  component_protocol_declares_non_convexity_and_returns_general_clauses();
  incremental_component_protocol_is_backend_neutral_and_backtrackable();
  finds_visible_interface_constants_and_equalities_by_sort();
  ignores_boolean_and_internal_constants();
  deduplicates_reversed_visible_equality_atoms();
  ignores_trivial_self_equalities_as_interface_pairs();
  registry_supports_mock_theory_dispatch_without_creating_atoms();
  manager_tracks_canonical_interface_equality_values();
  manager_rejects_unregistered_or_cross_sort_equality_values();
  manager_rebuild_replaces_previous_arrangement();
  manager_rolls_back_interface_equality_values_by_decision_level();
  manager_tracks_interface_equality_assignment_snapshots();
  manager_rolls_back_interface_equality_assignments_by_decision_level();
  manager_tracks_per_round_interface_equality_classifications();
  manager_owns_round_scoped_shared_model_state();
  manager_rebuilds_registry_from_explicit_interface_terms();
  manager_owns_lazy_shared_equality_decisions();
  reports_real_terms_inside_euf_atoms_as_purification_sites();
  reports_uninterpreted_applications_inside_arithmetic_atoms();
  model_coordinator_accepts_agreeing_current_snapshots();
  model_coordinator_rejects_stale_snapshots();
  model_coordinator_rejects_incomplete_projections_and_preserves_exceptions();
  model_coordinator_mutates_a_projection_to_agreement();
  model_coordinator_accepts_a_compatible_model_variant();
  model_coordinator_rejects_a_mutation_that_splits_an_unlisted_pair();
  model_coordinator_accepts_a_mutation_that_reaches_full_agreement();
  model_coordinator_turns_fixed_point_disagreement_into_a_choice();
  model_coordinator_rejects_unchanged_mutations();
  model_coordinator_names_a_mutation_that_does_not_advance_the_revision();
  model_coordinator_prioritizes_care_pair_disagreements();
  model_coordinator_batches_component_model_choices();
  model_coordinator_keeps_propagation_splitting_and_cancellation_typed();
  return 0;
}
