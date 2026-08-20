#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>

#include "sat/lit.hpp"
#include "theory/arithmetic/lia_gcd_consistency.hpp"
#include "theory/arithmetic/lra_tableau.hpp"
#include "util/rational.hpp"

using primo::sat::Lit;
using primo::theory::arithmetic::DeltaRational;
using primo::theory::arithmetic::gcd_inconsistent_row;
using primo::theory::arithmetic::LraTableau;
using primo::theory::arithmetic::LraVarId;
using primo::util::Rational;

namespace {

DeltaRational rat(long value) { return DeltaRational::from_rational(Rational(value)); }

// 2x - 2y = 1: 2x-2y is always even, so this row can never equal an odd
// value -- the motivating example from docs/qf-lia-branch-and-bound.md.
void catches_the_motivating_example() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(2)}, {y, Rational(-2)}});
  assert(!tableau.assert_upper(s, rat(1), Lit{1}).has_value());
  assert(!tableau.assert_lower(s, rat(1), Lit{2}).has_value());
  assert(!tableau.check().has_value());

  // Which variable ends up basic (and so which row gets checked) is a
  // Bland's-rule/pivot implementation detail, not part of the contract --
  // only that *some* row catches the inconsistency.
  assert(gcd_inconsistent_row(tableau).has_value());
}

// The same relationship, but stored the way PreparedLraTheory's
// scale-canonicalization actually leaves it: coefficients divided down to
// (1, -1), bound divided down to 1/2. Must catch this identically to the
// integer-coefficient form above -- this is the specific case stage 1's
// bound_assertion() bug (see docs/qf-lia-branch-and-bound.md) already
// showed canonicalization can quietly break a naive integer check on.
void catches_the_canonicalized_form_of_the_same_row() {
  // rat() only builds integers; the canonicalized bound is 1/2.
  const DeltaRational half = DeltaRational::from_rational(Rational(1, 2));
  LraTableau canonical_tableau;
  const LraVarId cx = canonical_tableau.add_variable();
  const LraVarId cy = canonical_tableau.add_variable();
  const LraVarId cs = canonical_tableau.add_variable();
  canonical_tableau.add_row(cs, {{cx, Rational(1)}, {cy, Rational(-1)}});
  assert(!canonical_tableau.assert_upper(cs, half, Lit{1}).has_value());
  assert(!canonical_tableau.assert_lower(cs, half, Lit{2}).has_value());
  assert(!canonical_tableau.check().has_value());

  assert(gcd_inconsistent_row(canonical_tableau).has_value());
}

// 2x + 3y = 1: gcd(2,3) = 1 divides everything, so every fixed value is
// reachable (e.g. x=-1, y=1). Must NOT be flagged -- the critical
// soundness check: a false positive here would mean reporting Unsat on a
// satisfiable instance.
void does_not_flag_a_coprime_row() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(2)}, {y, Rational(3)}});
  assert(!tableau.assert_upper(s, rat(1), Lit{1}).has_value());
  assert(!tableau.assert_lower(s, rat(1), Lit{2}).has_value());
  assert(!tableau.check().has_value());

  assert(!gcd_inconsistent_row(tableau).has_value());
}

// Same inconsistent row as the motivating example, but bounded on only
// one side (not fixed) -- nothing to check yet, must be skipped rather
// than flagged.
void skips_a_row_that_is_bounded_but_not_fixed() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(2)}, {y, Rational(-2)}});
  assert(!tableau.assert_upper(s, rat(1), Lit{1}).has_value());
  assert(!tableau.check().has_value());

  assert(!gcd_inconsistent_row(tableau).has_value());
}

// A row with a single coefficient-1 term (the common shape after
// scale-canonicalization divides a two-term row down): gcd is trivially
// 1, must never be flagged regardless of the fixed value.
void does_not_flag_a_trivial_coefficient_one_row() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}});
  assert(!tableau.assert_upper(s, rat(7), Lit{1}).has_value());
  assert(!tableau.assert_lower(s, rat(7), Lit{2}).has_value());
  assert(!tableau.check().has_value());

  assert(!gcd_inconsistent_row(tableau).has_value());
}

// A fixed row equal to zero is always achievable (every variable at 0),
// regardless of the row's coefficients.
void does_not_flag_a_row_fixed_to_zero() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(4)}, {y, Rational(-6)}});
  assert(!tableau.assert_upper(s, rat(0), Lit{1}).has_value());
  assert(!tableau.assert_lower(s, rat(0), Lit{2}).has_value());
  assert(!tableau.check().has_value());

  assert(!gcd_inconsistent_row(tableau).has_value());
}

}  // namespace

int main() {
  catches_the_motivating_example();
  catches_the_canonicalized_form_of_the_same_row();
  does_not_flag_a_coprime_row();
  skips_a_row_that_is_bounded_but_not_fixed();
  does_not_flag_a_trivial_coefficient_one_row();
  does_not_flag_a_row_fixed_to_zero();
  return 0;
}
