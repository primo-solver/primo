#include <cassert>
#include <vector>

#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "smt/preprocessing.hpp"

namespace {

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

  assert(result.accepted_sets == 1);
  assert(result.values == 3);
  assert(result.terms == 3);
  assert(result.rejected_noninvariant == 0);
  assert(result.constraints.size() == 6);
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

  assert(result.accepted_sets == 0);
  assert(result.rejected_noninvariant == 1);
  assert(result.constraints.empty());
}

void moving_terms_are_rejected() {
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

  assert(result.accepted_sets == 0);
  assert(result.rejected_noninvariant == 1);
  assert(result.constraints.empty());
}

}  // namespace

int main() {
  symmetric_fixed_terms_get_value_precedence();
  asymmetric_value_use_is_rejected();
  moving_terms_are_rejected();
  return 0;
}
