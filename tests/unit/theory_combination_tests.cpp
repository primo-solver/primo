#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <vector>

#include "core/ids.hpp"
#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "smt/theory_combination.hpp"
#include "util/rational.hpp"

using primo::core::SymbolTable;
using primo::core::TermManager;
using primo::smt::analyze_nelson_oppen_interface;
using primo::smt::InterfaceEqualityAssignment;
using primo::smt::InterfaceEqualityClassification;
using primo::smt::InterfaceEqualityStatus;
using primo::smt::InterfaceEqualityValue;
using primo::smt::PurificationSiteKind;
using primo::smt::TheoryCombinationManager;

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

}  // namespace

int main() {
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
  manager_rebuilds_registry_from_explicit_interface_terms();
  reports_real_terms_inside_euf_atoms_as_purification_sites();
  reports_uninterpreted_applications_inside_arithmetic_atoms();
  return 0;
}
