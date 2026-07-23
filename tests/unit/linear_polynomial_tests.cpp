#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>

#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "theory/arithmetic/linear_polynomial.hpp"
#include "util/rational.hpp"

using primo::core::SymbolTable;
using primo::core::TermId;
using primo::core::TermManager;
using primo::theory::arithmetic::LinearPolynomial;
using primo::util::Rational;

namespace {

Rational coefficient_of(const LinearPolynomial& poly, TermId variable) {
  const auto found = poly.terms().find(variable);
  return found == poly.terms().end() ? Rational(0) : found->second;
}

void bare_variable_has_unit_coefficient() {
  SymbolTable symbols;
  const auto x_symbol = symbols.declare_constant("x", symbols.real_sort());
  TermManager terms(symbols);
  const auto x = terms.mk_constant(x_symbol);

  const auto poly = LinearPolynomial::from_term(terms, x);
  assert(poly.terms().size() == 1);
  assert(coefficient_of(poly, x) == Rational(1));
  assert(poly.constant() == Rational(0));
  assert(!poly.is_constant());
}

void bare_rational_is_pure_constant() {
  SymbolTable symbols;
  TermManager terms(symbols);
  const auto three = terms.mk_rational(Rational(3));

  const auto poly = LinearPolynomial::from_term(terms, three);
  assert(poly.is_constant());
  assert(poly.constant() == Rational(3));
}

void plus_sums_coefficients_of_the_same_variable() {
  SymbolTable symbols;
  const auto x_symbol = symbols.declare_constant("x", symbols.real_sort());
  const auto y_symbol = symbols.declare_constant("y", symbols.real_sort());
  TermManager terms(symbols);
  const auto x = terms.mk_constant(x_symbol);
  const auto y = terms.mk_constant(y_symbol);

  // (+ x (* 2 x) y 3) => 3x + y + 3
  const auto sum =
      terms.mk_plus({x, terms.mk_times(Rational(2), x), y, terms.mk_rational(Rational(3))});
  const auto poly = LinearPolynomial::from_term(terms, sum);
  assert(poly.terms().size() == 2);
  assert(coefficient_of(poly, x) == Rational(3));
  assert(coefficient_of(poly, y) == Rational(1));
  assert(poly.constant() == Rational(3));
}

void times_distributes_over_a_compound_child() {
  SymbolTable symbols;
  const auto x_symbol = symbols.declare_constant("x", symbols.real_sort());
  const auto y_symbol = symbols.declare_constant("y", symbols.real_sort());
  TermManager terms(symbols);
  const auto x = terms.mk_constant(x_symbol);
  const auto y = terms.mk_constant(y_symbol);

  // (* 2 (+ x y 1)) => 2x + 2y + 2
  const auto inner = terms.mk_plus({x, y, terms.mk_rational(Rational(1))});
  const auto scaled = terms.mk_times(Rational(2), inner);
  const auto poly = LinearPolynomial::from_term(terms, scaled);
  assert(coefficient_of(poly, x) == Rational(2));
  assert(coefficient_of(poly, y) == Rational(2));
  assert(poly.constant() == Rational(2));
}

void opposite_coefficients_cancel_out() {
  SymbolTable symbols;
  const auto x_symbol = symbols.declare_constant("x", symbols.real_sort());
  TermManager terms(symbols);
  const auto x = terms.mk_constant(x_symbol);

  // (+ x (* -1 x)) => 0
  const auto sum = terms.mk_plus({x, terms.mk_times(Rational(-1), x)});
  const auto poly = LinearPolynomial::from_term(terms, sum);
  assert(poly.is_constant());
  assert(poly.constant() == Rational(0));
}

void add_and_scale_match_from_term() {
  SymbolTable symbols;
  const auto x_symbol = symbols.declare_constant("x", symbols.real_sort());
  const auto y_symbol = symbols.declare_constant("y", symbols.real_sort());
  TermManager terms(symbols);
  const auto x = terms.mk_constant(x_symbol);
  const auto y = terms.mk_constant(y_symbol);

  LinearPolynomial lhs;
  lhs.add_term(x, Rational(1));
  lhs.add_constant(Rational(1));
  LinearPolynomial rhs;
  rhs.add_term(y, Rational(1));
  lhs.add(rhs);
  lhs.scale(Rational(2));
  // 2*((x + 1) + y) = 2x + 2y + 2
  assert(coefficient_of(lhs, x) == Rational(2));
  assert(coefficient_of(lhs, y) == Rational(2));
  assert(lhs.constant() == Rational(2));

  const auto expected_term =
      terms.mk_times(Rational(2), terms.mk_plus({x, terms.mk_rational(Rational(1)), y}));
  const auto expected = LinearPolynomial::from_term(terms, expected_term);
  assert(coefficient_of(lhs, x) == coefficient_of(expected, x));
  assert(coefficient_of(lhs, y) == coefficient_of(expected, y));
  assert(lhs.constant() == expected.constant());
}

}  // namespace

int main() {
  bare_variable_has_unit_coefficient();
  bare_rational_is_pure_constant();
  plus_sums_coefficients_of_the_same_variable();
  times_distributes_over_a_compound_child();
  opposite_coefficients_cancel_out();
  add_and_scale_match_from_term();
  return 0;
}
