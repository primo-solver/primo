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
using primo::smt::InterfaceEqualityStatus;
using primo::smt::PurificationSiteKind;

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
  assert(summary.equality_registry.size() == 3);
  assert(summary.missing_equalities == 1);
  assert(summary.visible_equalities.size() == 2);
  assert(summary.visible_equalities[0].lhs == a);
  assert(summary.visible_equalities[0].rhs == b);
  assert(summary.visible_equalities[1].lhs == b);
  assert(summary.visible_equalities[1].rhs == c);
  assert(summary.equality_registry[0].lhs == a);
  assert(summary.equality_registry[0].rhs == b);
  assert(summary.equality_registry[0].status == InterfaceEqualityStatus::Visible);
  assert(summary.equality_registry[1].lhs == a);
  assert(summary.equality_registry[1].rhs == c);
  assert(summary.equality_registry[1].status == InterfaceEqualityStatus::Missing);
  assert(summary.equality_registry[2].lhs == b);
  assert(summary.equality_registry[2].rhs == c);
  assert(summary.equality_registry[2].status == InterfaceEqualityStatus::Visible);
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
  assert(summary.equality_registry.size() == 3);
  assert(mock.visible_pairs == 2);
  assert(mock.missing_pairs == 1);
  const std::vector expected_visible_atoms{eq_ab, terms.mk_eq(a, c)};
  assert(mock.visible_atoms == expected_visible_atoms);
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
  reports_real_terms_inside_euf_atoms_as_purification_sites();
  reports_uninterpreted_applications_inside_arithmetic_atoms();
  return 0;
}
