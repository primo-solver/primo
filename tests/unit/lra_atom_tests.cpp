#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <stdexcept>

#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "theory/arithmetic/lra_atom.hpp"
#include "util/rational.hpp"

using primo::core::SymbolTable;
using primo::core::TermManager;
using primo::theory::arithmetic::LraAtom;
using primo::theory::arithmetic::LraRelation;
using primo::theory::arithmetic::normalize_atom;
using primo::util::Rational;

namespace {

template <typename Fn>
void expect_invalid_argument(Fn fn) {
  try {
    fn();
  } catch (const std::invalid_argument&) {
    return;
  }
  assert(false && "expected std::invalid_argument");
}

void moves_constants_to_the_right_hand_side() {
  SymbolTable symbols;
  const auto x_symbol = symbols.declare_constant("x", symbols.real_sort());
  TermManager terms(symbols);
  const auto x = terms.mk_constant(x_symbol);

  // (<= (+ x 3) 10)  =>  x <= 7
  const auto lhs = terms.mk_plus({x, terms.mk_rational(Rational(3))});
  const auto atom_term = terms.mk_le(lhs, terms.mk_rational(Rational(10)));
  const LraAtom atom = normalize_atom(terms, atom_term);
  assert(atom.relation == LraRelation::Le);
  assert(atom.polynomial.constant() == Rational(0));
  assert(atom.polynomial.terms().size() == 1);
  assert(atom.polynomial.terms().at(x) == Rational(1));
  assert(atom.rhs == Rational(7));
}

void moves_variables_from_both_sides() {
  SymbolTable symbols;
  const auto x_symbol = symbols.declare_constant("x", symbols.real_sort());
  const auto y_symbol = symbols.declare_constant("y", symbols.real_sort());
  TermManager terms(symbols);
  const auto x = terms.mk_constant(x_symbol);
  const auto y = terms.mk_constant(y_symbol);

  // (< (+ x 1) (+ y 4))  =>  x - y < 3
  const auto lhs = terms.mk_plus({x, terms.mk_rational(Rational(1))});
  const auto rhs = terms.mk_plus({y, terms.mk_rational(Rational(4))});
  const auto atom_term = terms.mk_lt(lhs, rhs);
  const LraAtom atom = normalize_atom(terms, atom_term);
  assert(atom.relation == LraRelation::Lt);
  assert(atom.polynomial.terms().size() == 2);
  assert(atom.polynomial.terms().at(x) == Rational(1));
  assert(atom.polynomial.terms().at(y) == Rational(-1));
  assert(atom.rhs == Rational(3));
}

void equality_relation_is_preserved() {
  SymbolTable symbols;
  const auto x_symbol = symbols.declare_constant("x", symbols.real_sort());
  TermManager terms(symbols);
  const auto x = terms.mk_constant(x_symbol);

  const auto atom_term = terms.mk_eq(x, terms.mk_rational(Rational(2)));
  const LraAtom atom = normalize_atom(terms, atom_term);
  assert(atom.relation == LraRelation::Eq);
  assert(atom.polynomial.terms().at(x) == Rational(1));
  assert(atom.rhs == Rational(2));
}

// normalize_atom is sort-agnostic: an Int atom's strictness is left alone
// here and handled later, in check_sat.cpp's bound_assertion() (see that
// function's comment for why -- scale-canonicalization runs between this
// function and bound_assertion, and can turn an integer rhs fractional).
void int_atoms_are_normalized_like_real_ones() {
  SymbolTable symbols;
  const auto x_symbol = symbols.declare_constant("x", symbols.int_sort());
  TermManager terms(symbols);
  const auto x = terms.mk_constant(x_symbol);

  const auto lt_atom = terms.mk_lt(x, terms.mk_rational(Rational(5), symbols.int_sort()));
  const LraAtom lt_normalized = normalize_atom(terms, lt_atom);
  assert(lt_normalized.relation == LraRelation::Lt);
  assert(lt_normalized.rhs == Rational(5));

  const auto le_atom = terms.mk_le(x, terms.mk_rational(Rational(5), symbols.int_sort()));
  assert(normalize_atom(terms, le_atom).relation == LraRelation::Le);
  assert(normalize_atom(terms, le_atom).rhs == Rational(5));
}

void rejects_non_atom_terms() {
  SymbolTable symbols;
  const auto p_symbol = symbols.declare_constant("p", symbols.bool_sort());
  TermManager terms(symbols);
  const auto p = terms.mk_constant(p_symbol);
  expect_invalid_argument([&] { normalize_atom(terms, p); });
}

}  // namespace

int main() {
  moves_constants_to_the_right_hand_side();
  moves_variables_from_both_sides();
  equality_relation_is_preserved();
  int_atoms_are_normalized_like_real_ones();
  rejects_non_atom_terms();
  return 0;
}
