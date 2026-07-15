#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "smt/preprocessing.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    std::abort();
  }
}

struct Fixture {
  primo::core::SymbolTable symbols;
  primo::core::SortId sort = symbols.declare_sort("U");
  primo::core::SymbolId a = symbols.declare_constant("a", sort);
  primo::core::SymbolId b = symbols.declare_constant("b", sort);
  primo::core::SymbolId c = symbols.declare_constant("c", sort);
  primo::core::SymbolId x = symbols.declare_constant("x", sort);
  primo::core::SymbolId y = symbols.declare_constant("y", sort);
  primo::core::SymbolId z = symbols.declare_constant("z", sort);
  primo::core::SymbolId f = symbols.declare_function("f", {sort}, sort);
  primo::core::SymbolId p = symbols.declare_predicate("P", {sort});
  primo::core::TermManager terms{symbols};

  primo::core::TermId constant(primo::core::SymbolId symbol) { return terms.mk_constant(symbol); }

  primo::core::TermId app(primo::core::SymbolId symbol, primo::core::TermId arg) {
    return terms.mk_app(symbol, {arg});
  }

  primo::core::TermId pred(primo::core::SymbolId symbol, primo::core::TermId arg) {
    return terms.mk_app(symbol, {arg});
  }

  primo::core::TermId neq(primo::core::TermId lhs, primo::core::TermId rhs) {
    return terms.mk_not(terms.mk_eq(lhs, rhs));
  }

  primo::core::TermId choices(primo::core::TermId term,
                              const std::vector<primo::core::TermId>& values) {
    std::vector<primo::core::TermId> equalities;
    for (const primo::core::TermId value : values) {
      equalities.push_back(terms.mk_eq(term, value));
    }
    return terms.mk_or(std::move(equalities));
  }

  std::vector<primo::core::TermId> value_disequalities() {
    const primo::core::TermId a_term = constant(a);
    const primo::core::TermId b_term = constant(b);
    const primo::core::TermId c_term = constant(c);
    return {neq(a_term, b_term), neq(a_term, c_term), neq(b_term, c_term)};
  }
};

void symmetric_fixed_terms_get_value_precedence() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const std::vector<primo::core::TermId> values{a, b, c};

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.terms.mk_or(
      {fixture.pred(fixture.p, a), fixture.pred(fixture.p, b), fixture.pred(fixture.p, c)}));
  assertions.push_back(fixture.choices(fixture.constant(fixture.x), values));
  assertions.push_back(fixture.choices(fixture.constant(fixture.y), values));
  assertions.push_back(fixture.choices(fixture.constant(fixture.z), values));

  primo::smt::UfSymmetryBreaker breaker(fixture.terms);
  const primo::smt::UfSymmetryBreakingResult result = breaker.break_symmetries(assertions);

  check(result.accepted_sets == 1);
  check(result.values == 3);
  check(result.terms == 3);
  check(result.rejected_noninvariant == 0);
  check(result.constraints.size() == 6);
}

void asymmetric_value_use_is_rejected() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const std::vector<primo::core::TermId> values{a, b, c};

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.pred(fixture.p, a));
  assertions.push_back(fixture.choices(fixture.constant(fixture.x), values));
  assertions.push_back(fixture.choices(fixture.constant(fixture.y), values));
  assertions.push_back(fixture.choices(fixture.constant(fixture.z), values));

  primo::smt::UfSymmetryBreaker breaker(fixture.terms);
  const primo::smt::UfSymmetryBreakingResult result = breaker.break_symmetries(assertions);

  check(result.accepted_sets == 0);
  check(result.rejected_noninvariant == 1);
  check(result.constraints.empty());
}

void moving_terms_keep_used_values_and_get_paper_restrictions() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const std::vector<primo::core::TermId> values{a, b, c};

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.choices(fixture.app(fixture.f, a), values));
  assertions.push_back(fixture.choices(fixture.app(fixture.f, b), values));
  assertions.push_back(fixture.choices(fixture.app(fixture.f, c), values));

  primo::smt::UfSymmetryBreaker breaker(fixture.terms);
  const primo::smt::UfSymmetryBreakingResult result = breaker.break_symmetries(assertions);

  check(result.accepted_sets == 1);
  check(result.values == 3);
  check(result.terms == 3);
  check(result.rejected_noninvariant == 0);
  check(result.constraints.size() == 1);
}

void mixed_fixed_and_moving_terms_use_fixed_value_precedence_only() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const std::vector<primo::core::TermId> values{a, b, c};

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.choices(fixture.constant(fixture.x), values));
  assertions.push_back(fixture.choices(fixture.constant(fixture.y), values));
  assertions.push_back(fixture.choices(fixture.constant(fixture.z), values));
  assertions.push_back(fixture.choices(fixture.app(fixture.f, a), values));
  assertions.push_back(fixture.choices(fixture.app(fixture.f, b), values));
  assertions.push_back(fixture.choices(fixture.app(fixture.f, c), values));

  primo::smt::UfSymmetryBreaker breaker(fixture.terms);
  const primo::smt::UfSymmetryBreakingResult result = breaker.break_symmetries(assertions);

  check(result.accepted_sets == 1);
  check(result.values == 3);
  check(result.terms == 3);
  check(result.rejected_noninvariant == 0);
  check(result.constraints.size() == 6);
}

void equality_bridge_derives_common_branch_consequence() {
  Fixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId z = fixture.constant(fixture.z);
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);

  const primo::core::TermId left =
      fixture.terms.mk_and({fixture.terms.mk_eq(x, y), fixture.terms.mk_eq(y, z)});
  const primo::core::TermId right =
      fixture.terms.mk_and({fixture.terms.mk_eq(x, a), fixture.terms.mk_eq(a, z)});
  const primo::core::TermId unrelated =
      fixture.terms.mk_and({fixture.terms.mk_eq(x, b), fixture.terms.mk_eq(b, z)});
  const std::vector<primo::core::TermId> assertions{fixture.terms.mk_or({left, right, unrelated})};

  primo::smt::EqualityBridge bridge(fixture.terms);
  const primo::smt::EqualityBridgeResult result = bridge.bridge(assertions);

  check(result.skipped_or_nodes == 0);
  check(result.equalities.size() == 1);
  const primo::core::Term& derived = fixture.terms.get(result.equalities.front());
  check(derived.kind == primo::core::TermKind::Equality);
  check((derived.children == std::vector{x, z} || derived.children == std::vector{z, x}));
}

void unit_equality_propagation_simplifies_disequality() {
  Fixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId z = fixture.constant(fixture.z);

  const std::vector<primo::core::TermId> assertions{
      fixture.terms.mk_eq(x, y),
      fixture.terms.mk_eq(y, z),
      fixture.terms.mk_not(fixture.terms.mk_eq(x, z)),
  };

  const std::vector<primo::core::TermId> simplified =
      primo::smt::propagate_unit_equalities(fixture.terms, assertions);

  check(simplified.size() == assertions.size() + 1);
  check(simplified[0] == assertions[0]);
  check(simplified[1] == assertions[1]);
  check(simplified[2] == assertions[2]);
  check(fixture.terms.get(simplified[3]).kind == primo::core::TermKind::BoolValue);
  check(!fixture.terms.get(simplified[3]).bool_value);
}

void unit_equality_propagation_simplifies_nested_boolean_terms_after_interning() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId pa = fixture.pred(fixture.p, a);
  const primo::core::TermId pb = fixture.pred(fixture.p, b);

  const primo::core::TermId inner_lhs = fixture.terms.mk_and({fixture.terms.mk_eq(x, y), pa});
  const primo::core::TermId inner = fixture.terms.mk_implies(inner_lhs, pb);
  const primo::core::TermId outer =
      fixture.terms.mk_implies(inner, fixture.terms.mk_not(fixture.terms.mk_eq(a, b)));
  const std::vector<primo::core::TermId> assertions{fixture.terms.mk_eq(x, y), outer};

  std::size_t consequences = 0;
  const std::vector<primo::core::TermId> simplified =
      primo::smt::propagate_unit_equalities(fixture.terms, assertions, &consequences);

  check(!simplified.empty());
  check(consequences >= 1);
}

void ground_unit_substitution_rewrites_applications_and_preserves_units() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId fa = fixture.app(fixture.f, a);
  const primo::core::TermId pc = fixture.pred(fixture.p, c);
  const primo::core::TermId unit = fixture.terms.mk_eq(fa, b);
  const primo::core::TermId formula = fixture.terms.mk_or({fixture.terms.mk_eq(fa, c), pc});
  const std::vector<primo::core::TermId> assertions{unit, formula};

  const primo::smt::GroundEqualitySubstitutionResult result =
      primo::smt::substitute_ground_unit_equalities(fixture.terms, assertions);

  check(result.units == 1);
  check(result.rewrites >= 1);
  check(std::find(result.assertions.begin(), result.assertions.end(), unit) !=
        result.assertions.end());
  const primo::core::TermId rewritten = fixture.terms.mk_or({fixture.terms.mk_eq(b, c), pc});
  check(std::find(result.assertions.begin(), result.assertions.end(), rewritten) !=
        result.assertions.end());
}

void ground_unit_substitution_uses_conjunctive_units_and_drops_true_parts() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId fa = fixture.app(fixture.f, a);
  const primo::core::TermId unit = fixture.terms.mk_eq(fa, b);
  const primo::core::TermId assertion =
      fixture.terms.mk_and({unit, fixture.terms.mk_not(fixture.terms.mk_eq(fa, c))});

  const primo::smt::GroundEqualitySubstitutionResult result =
      primo::smt::substitute_ground_unit_equalities(fixture.terms, {assertion});

  check(result.units == 1);
  check(result.rewrites >= 1);
  check(std::find(result.assertions.begin(), result.assertions.end(), unit) !=
        result.assertions.end());
  const primo::core::TermId rewritten = fixture.terms.mk_not(fixture.terms.mk_eq(b, c));
  check(std::find(result.assertions.begin(), result.assertions.end(), rewritten) !=
        result.assertions.end());
}

void boolean_unit_propagation_simplifies_boolean_structure_and_arguments() {
  Fixture fixture;
  const primo::core::TermId p =
      fixture.constant(fixture.symbols.declare_constant("flag", fixture.symbols.bool_sort()));
  const primo::core::TermId true_term = fixture.terms.mk_bool(true);
  const primo::core::SymbolId g =
      fixture.symbols.declare_function("g", {fixture.symbols.bool_sort()}, fixture.sort);
  const primo::core::TermId gp = fixture.terms.mk_app(g, {p});
  const primo::core::TermId gt = fixture.terms.mk_app(g, {true_term});
  const std::vector<primo::core::TermId> assertions{
      p,
      fixture.terms.mk_or(
          {fixture.terms.mk_not(p), fixture.pred(fixture.p, fixture.constant(fixture.a))}),
      fixture.terms.mk_not(fixture.terms.mk_eq(gp, gt)),
  };

  const primo::smt::BooleanUnitPropagationResult result =
      primo::smt::propagate_boolean_units(fixture.terms, assertions);

  check(result.units == 1);
  check(result.rewrites >= 2);
  check(result.assertions.size() == 3);
  const primo::core::TermId pa = fixture.pred(fixture.p, fixture.constant(fixture.a));
  const primo::core::TermId disequality = fixture.terms.mk_not(fixture.terms.mk_eq(gp, gt));
  check(std::find(result.assertions.begin(), result.assertions.end(), p) !=
        result.assertions.end());
  check(std::find(result.assertions.begin(), result.assertions.end(), pa) !=
        result.assertions.end());
  check(std::find(result.assertions.begin(), result.assertions.end(), disequality) !=
        result.assertions.end());
}

void boolean_unit_propagation_preserves_contradictory_units() {
  Fixture fixture;
  const primo::core::TermId p =
      fixture.constant(fixture.symbols.declare_constant("flag", fixture.symbols.bool_sort()));
  const std::vector<primo::core::TermId> assertions{
      p, fixture.terms.mk_not(p),
      fixture.terms.mk_or(
          {fixture.terms.mk_not(p), fixture.pred(fixture.p, fixture.constant(fixture.a))})};

  const primo::smt::BooleanUnitPropagationResult result =
      primo::smt::propagate_boolean_units(fixture.terms, assertions);

  check(result.units == 0);
  check(result.rewrites == 0);
  check(result.assertions == assertions);
}

void forced_atom_propagation_simplifies_predicate_units() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId d =
      fixture.constant(fixture.symbols.declare_constant("d", fixture.sort));
  const primo::core::TermId pa = fixture.pred(fixture.p, a);
  const primo::core::TermId cd = fixture.terms.mk_eq(c, d);
  const std::vector<primo::core::TermId> assertions{
      pa, fixture.terms.mk_or({fixture.terms.mk_not(pa), cd})};

  const primo::smt::ForcedAtomPropagationResult result =
      primo::smt::propagate_forced_atoms(fixture.terms, assertions);

  check(result.units == 1);
  check(result.rewrites >= 1);
  check(std::find(result.assertions.begin(), result.assertions.end(), pa) !=
        result.assertions.end());
  check(std::find(result.assertions.begin(), result.assertions.end(), cd) !=
        result.assertions.end());
}

void forced_atom_propagation_uses_negative_equality_units() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId d =
      fixture.constant(fixture.symbols.declare_constant("d", fixture.sort));
  const primo::core::TermId ab = fixture.terms.mk_eq(a, b);
  const primo::core::TermId cd = fixture.terms.mk_eq(c, d);
  const std::vector<primo::core::TermId> assertions{fixture.terms.mk_not(ab),
                                                    fixture.terms.mk_or({ab, cd})};

  const primo::smt::ForcedAtomPropagationResult result =
      primo::smt::propagate_forced_atoms(fixture.terms, assertions);

  check(result.units == 1);
  check(result.disequalities == 1);
  check(result.rewrites >= 1);
  check(std::find(result.assertions.begin(), result.assertions.end(), fixture.terms.mk_not(ab)) !=
        result.assertions.end());
  check(std::find(result.assertions.begin(), result.assertions.end(), cd) !=
        result.assertions.end());
}

void forced_atom_propagation_folds_transitive_disequalities() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId ab = fixture.terms.mk_eq(a, b);
  const primo::core::TermId bc = fixture.terms.mk_eq(b, c);
  const primo::core::TermId ac = fixture.terms.mk_eq(a, c);
  const std::vector<primo::core::TermId> assertions{ab, bc, fixture.terms.mk_not(ac)};

  const primo::smt::ForcedAtomPropagationResult result =
      primo::smt::propagate_forced_atoms(fixture.terms, assertions);

  check(result.units == 3);
  check(result.disequalities == 1);
  check(result.rewrites >= 1);
  check(std::find(result.assertions.begin(), result.assertions.end(),
                  fixture.terms.mk_bool(false)) != result.assertions.end());
}

void forced_atom_propagation_preserves_contradictory_exact_units() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId pa = fixture.pred(fixture.p, a);
  const primo::core::TermId pb = fixture.pred(fixture.p, fixture.constant(fixture.b));
  const std::vector<primo::core::TermId> assertions{
      pa, fixture.terms.mk_not(pa), fixture.terms.mk_or({fixture.terms.mk_not(pa), pb})};

  const primo::smt::ForcedAtomPropagationResult result =
      primo::smt::propagate_forced_atoms(fixture.terms, assertions);

  check(result.units == 0);
  check(result.rewrites == 0);
  check(result.assertions == assertions);
}

void finite_domain_at_most_one_adds_pairwise_clauses() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId x = fixture.constant(fixture.x);
  const std::vector<primo::core::TermId> values{a, b, c};

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.choices(x, values));

  primo::smt::FiniteDomainAtMostOne amo(fixture.terms);
  const primo::smt::FiniteDomainAtMostOneResult result = amo.generate(assertions);

  check(result.domains == 1);
  check(result.clauses.size() == 3);
  for (const primo::core::TermId clause : result.clauses) {
    const primo::core::Term& node = fixture.terms.get(clause);
    check(node.kind == primo::core::TermKind::Or);
    check(node.children.size() == 2);
  }

  assertions.insert(assertions.end(), result.clauses.begin(), result.clauses.end());
  const primo::smt::FiniteDomainAtMostOneResult repeated = amo.generate(assertions);
  check(repeated.domains == 0);
  check(repeated.clauses.empty());
}

void finite_domain_choices_may_be_nested_or_nodes() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId x = fixture.constant(fixture.x);

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.terms.mk_or(
      {fixture.terms.mk_eq(x, a),
       fixture.terms.mk_or({fixture.terms.mk_eq(x, b), fixture.terms.mk_eq(x, c)})}));

  primo::smt::FiniteDomainAtMostOne amo(fixture.terms);
  const primo::smt::FiniteDomainAtMostOneResult result = amo.generate(assertions);

  check(result.domains == 1);
  check(result.clauses.size() == 3);
}

void finite_domain_observed_equalities_get_pairwise_amo_clauses_without_choices() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId x = fixture.constant(fixture.x);

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.terms.mk_eq(x, a));
  assertions.push_back(fixture.terms.mk_eq(x, b));
  assertions.push_back(fixture.terms.mk_eq(x, c));

  primo::smt::FiniteDomainAtMostOne amo(fixture.terms);
  const primo::smt::FiniteDomainAtMostOneResult result = amo.generate(assertions);

  check(result.domains == 1);
  check(result.clauses.size() == 3);
  for (const primo::core::TermId clause : result.clauses) {
    const primo::core::Term& node = fixture.terms.get(clause);
    check(node.kind == primo::core::TermKind::Or);
    check(node.children.size() == 2);
  }

  assertions.insert(assertions.end(), result.clauses.begin(), result.clauses.end());
  const primo::smt::FiniteDomainAtMostOneResult repeated = amo.generate(assertions);
  check(repeated.domains == 0);
  check(repeated.clauses.empty());
}

void finite_domain_distinct_endpoints_get_value_amo_clauses() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const std::vector<primo::core::TermId> values{a, b, c};

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.choices(x, values));
  assertions.push_back(fixture.choices(y, values));
  assertions.push_back(fixture.neq(x, y));

  primo::smt::FiniteDomainAtMostOne amo(fixture.terms);
  const primo::smt::FiniteDomainAtMostOneResult result = amo.generate(assertions);

  check(result.domains == 2);
  check(result.clauses.size() == 9);
}

void finite_domain_equality_definitions_connect_terms_with_same_domain() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId c = fixture.constant(fixture.c);
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const std::vector<primo::core::TermId> values{a, b, c};

  std::vector<primo::core::TermId> assertions = fixture.value_disequalities();
  assertions.push_back(fixture.choices(x, values));
  assertions.push_back(fixture.choices(y, values));

  primo::smt::FiniteDomainEqualityDefinitions definitions(fixture.terms);
  const primo::smt::FiniteDomainEqualityDefinitionsResult result = definitions.generate(assertions);

  check(result.term_pairs == 1);
  check(result.clauses.size() == 9);
  for (const primo::core::TermId clause : result.clauses) {
    const primo::core::Term& node = fixture.terms.get(clause);
    check(node.kind == primo::core::TermKind::Or);
    check(node.children.size() == 3);
  }

  assertions.insert(assertions.end(), result.clauses.begin(), result.clauses.end());
  const primo::smt::FiniteDomainEqualityDefinitionsResult repeated =
      definitions.generate(assertions);
  check(repeated.term_pairs == 0);
  check(repeated.clauses.empty());
}

void congruence_definitions_connect_same_symbol_applications() {
  Fixture fixture;
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId y = fixture.constant(fixture.y);
  const primo::core::TermId fx = fixture.app(fixture.f, x);
  const primo::core::TermId fy = fixture.app(fixture.f, y);
  const std::vector<primo::core::TermId> assertions{fixture.terms.mk_or(
      {fixture.terms.mk_eq(x, y), fixture.terms.mk_not(fixture.terms.mk_eq(fx, fy))})};

  primo::smt::CongruenceDefinitionGenerator definitions(fixture.terms);
  const primo::smt::CongruenceDefinitionResult result = definitions.generate(assertions);

  check(result.groups == 1);
  check(result.skipped_groups == 0);
  check(result.clauses.size() == 1);
  const primo::core::Term& clause = fixture.terms.get(result.clauses.front());
  check(clause.kind == primo::core::TermKind::Or);
  check(clause.children.size() == 2);
}

void finite_domain_evaluator_simplifies_table_tautology() {
  Fixture fixture;
  const primo::core::TermId a = fixture.constant(fixture.a);
  const primo::core::TermId b = fixture.constant(fixture.b);
  const primo::core::TermId x = fixture.constant(fixture.x);
  const primo::core::TermId fx = fixture.app(fixture.f, x);
  const std::vector<primo::core::TermId> values{a, b};
  std::vector<primo::core::TermId> assertions{
      fixture.neq(a, b),
      fixture.choices(x, values),
      fixture.terms.mk_eq(fixture.app(fixture.f, a), a),
      fixture.terms.mk_eq(fixture.app(fixture.f, b), b),
      fixture.terms.mk_not(fixture.terms.mk_eq(fx, x)),
  };

  primo::smt::FiniteDomainEvaluator evaluator(fixture.terms);
  const primo::smt::FiniteDomainEvaluationResult result = evaluator.simplify(assertions);

  check(result.equalities_simplified >= 1);
  check(result.enumeration_cutoffs == 0);
  check(result.assertions.size() == assertions.size());
  const primo::core::Term& last = fixture.terms.get(result.assertions.back());
  check(last.kind == primo::core::TermKind::BoolValue);
  check(!last.bool_value);
}

}  // namespace

int main() {
  symmetric_fixed_terms_get_value_precedence();
  asymmetric_value_use_is_rejected();
  moving_terms_keep_used_values_and_get_paper_restrictions();
  mixed_fixed_and_moving_terms_use_fixed_value_precedence_only();
  equality_bridge_derives_common_branch_consequence();
  unit_equality_propagation_simplifies_disequality();
  unit_equality_propagation_simplifies_nested_boolean_terms_after_interning();
  ground_unit_substitution_rewrites_applications_and_preserves_units();
  ground_unit_substitution_uses_conjunctive_units_and_drops_true_parts();
  boolean_unit_propagation_simplifies_boolean_structure_and_arguments();
  boolean_unit_propagation_preserves_contradictory_units();
  forced_atom_propagation_simplifies_predicate_units();
  forced_atom_propagation_uses_negative_equality_units();
  forced_atom_propagation_folds_transitive_disequalities();
  forced_atom_propagation_preserves_contradictory_exact_units();
  finite_domain_at_most_one_adds_pairwise_clauses();
  finite_domain_choices_may_be_nested_or_nodes();
  finite_domain_observed_equalities_get_pairwise_amo_clauses_without_choices();
  finite_domain_distinct_endpoints_get_value_amo_clauses();
  finite_domain_equality_definitions_connect_terms_with_same_domain();
  congruence_definitions_connect_same_symbol_applications();
  finite_domain_evaluator_simplifies_table_tautology();
  return 0;
}
