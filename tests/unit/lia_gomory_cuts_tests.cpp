#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>

#include "sat/lit.hpp"
#include "theory/arithmetic/lia_gomory_cuts.hpp"
#include "theory/arithmetic/lra_tableau.hpp"
#include "util/rational.hpp"

using primo::sat::Lit;
using primo::theory::arithmetic::apply_gomory_cuts;
using primo::theory::arithmetic::DeltaRational;
using primo::theory::arithmetic::LraTableau;
using primo::theory::arithmetic::LraVarId;
using primo::util::Rational;

namespace {

DeltaRational rat(const Rational& value) { return DeltaRational::from_rational(value); }

// x_b = 2*x1 + 3*x2, x1 at lower bound 0, x2 at lower bound 1/2 (both
// J+, King's thesis eq. 4.9 terminology): x_b = 3/2, f0 = 1/2. Hand-derived
// cut (docs/qf-lia-branch-and-bound.md's stage 3 section has the full
// algebra): 4*x1 + 6*x2 >= 4. Independently verified by direct boundary
// search: (x1,x2) = (0, 1/2) -- the current fractional vertex -- gives
// 3 < 4 (correctly excluded); (0, 2/3) -- the nearest achievable integer
// point (x_b=2) -- gives exactly 4 (correctly included, tight).
void all_lower_bound_cut_tightens_without_resolving() {
  LraTableau tableau;
  const LraVarId x1 = tableau.add_variable();
  const LraVarId x2 = tableau.add_variable();
  const LraVarId x_b = tableau.add_variable();
  tableau.add_row(x_b, {{x1, Rational(2)}, {x2, Rational(3)}});
  assert(!tableau.assert_lower(x1, rat(Rational(0)), Lit{1}).has_value());
  assert(!tableau.assert_lower(x2, rat(Rational(1, 2)), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  assert(tableau.assignment(x_b) == rat(Rational(3, 2)));

  const bool infeasible = apply_gomory_cuts(tableau, {x_b}, 10);
  assert(!infeasible);

  // The cut must exclude the exact fractional vertex the cut was derived
  // from.
  std::size_t checkpoint = tableau.push();
  assert(!tableau.assert_upper(x1, rat(Rational(0)), Lit{3}).has_value());
  assert(!tableau.assert_upper(x2, rat(Rational(1, 2)), Lit{4}).has_value());
  assert(tableau.check().has_value());
  tableau.pop(checkpoint);

  // ...but must not exclude a genuine nearby integer-x_b point.
  checkpoint = tableau.push();
  assert(!tableau.assert_upper(x1, rat(Rational(0)), Lit{5}).has_value());
  assert(!tableau.assert_upper(x2, rat(Rational(2, 3)), Lit{6}).has_value());
  assert(!tableau.assert_lower(x2, rat(Rational(2, 3)), Lit{7}).has_value());
  assert(!tableau.check().has_value());
  tableau.pop(checkpoint);
}

// x_b = 2*x1 - 3*x2, x1 at lower bound 1/3 (J+), x2 at UPPER bound -1
// (K-, negative coefficient): x_b = 11/3, f0 = 2/3. Hand-derived cut:
// 6*x1 - 9*x2 >= 12. Verified the same way: (1/3, -1) -- the current
// vertex -- gives 11 < 12 (excluded); (1/3, -10/9) -- the nearest
// achievable integer point (x_b=4) -- gives exactly 12 (included, tight).
// x2's upper bound is asserted as a *negative* value specifically so the
// assignment (default 0) moves down onto it, landing "at the bound"
// without also fixing x2 (which would erase the distinction between the
// J and K code paths -- see the design note this test's counterpart in
// the design doc explains).
void mixed_lower_and_upper_bound_cut_tightens_without_resolving() {
  LraTableau tableau;
  const LraVarId x1 = tableau.add_variable();
  const LraVarId x2 = tableau.add_variable();
  const LraVarId x_b = tableau.add_variable();
  tableau.add_row(x_b, {{x1, Rational(2)}, {x2, Rational(-3)}});
  assert(!tableau.assert_lower(x1, rat(Rational(1, 3)), Lit{1}).has_value());
  assert(!tableau.assert_upper(x2, rat(Rational(-1)), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  assert(tableau.assignment(x_b) == rat(Rational(11, 3)));

  const bool infeasible = apply_gomory_cuts(tableau, {x_b}, 10);
  assert(!infeasible);

  std::size_t checkpoint = tableau.push();
  assert(!tableau.assert_upper(x1, rat(Rational(1, 3)), Lit{3}).has_value());
  assert(!tableau.assert_lower(x2, rat(Rational(-1)), Lit{4}).has_value());
  assert(tableau.check().has_value());
  tableau.pop(checkpoint);

  checkpoint = tableau.push();
  assert(!tableau.assert_upper(x1, rat(Rational(1, 3)), Lit{5}).has_value());
  assert(!tableau.assert_upper(x2, rat(Rational(-10, 9)), Lit{6}).has_value());
  assert(!tableau.assert_lower(x2, rat(Rational(-10, 9)), Lit{7}).has_value());
  assert(!tableau.check().has_value());
  tableau.pop(checkpoint);
}

// Same row/cut as the all-lower-bound case above, but x1 and x2 are
// FIXED (both bounds equal) to exactly the fractional vertex the cut
// excludes: the cut's slack is then forced to the single value 3, which
// directly conflicts with the newly asserted lower bound of 4 --
// apply_gomory_cuts must detect this itself (via its own check() call)
// and report the tableau infeasible, the same way branch_and_bound's own
// Unsat path is detected.
void cut_alone_proves_unsat() {
  LraTableau tableau;
  const LraVarId x1 = tableau.add_variable();
  const LraVarId x2 = tableau.add_variable();
  const LraVarId x_b = tableau.add_variable();
  tableau.add_row(x_b, {{x1, Rational(2)}, {x2, Rational(3)}});
  assert(!tableau.assert_lower(x1, rat(Rational(0)), Lit{1}).has_value());
  assert(!tableau.assert_upper(x1, rat(Rational(0)), Lit{2}).has_value());
  assert(!tableau.assert_lower(x2, rat(Rational(1, 2)), Lit{3}).has_value());
  assert(!tableau.assert_upper(x2, rat(Rational(1, 2)), Lit{4}).has_value());
  assert(!tableau.check().has_value());

  assert(apply_gomory_cuts(tableau, {x_b}, 10));
}

// x_b's row has a term (x1) sitting strictly *within* its bounds (not at
// either one) -- LraTableau's own documented deviation from "always at a
// bound." The whole row must be skipped, not partially cut: no cut slack
// gets added, and the call reports no infeasibility.
void row_with_a_term_not_at_a_bound_is_skipped() {
  LraTableau tableau;
  const LraVarId x1 = tableau.add_variable();
  const LraVarId x2 = tableau.add_variable();
  const LraVarId x_b = tableau.add_variable();
  tableau.add_row(x_b, {{x1, Rational(2)}, {x2, Rational(3)}});
  assert(!tableau.assert_lower(x1, rat(Rational(-5)), Lit{1}).has_value());
  assert(!tableau.assert_upper(x1, rat(Rational(5)), Lit{2}).has_value());
  assert(!tableau.assert_lower(x2, rat(Rational(1, 2)), Lit{3}).has_value());
  assert(!tableau.check().has_value());
  assert(tableau.assignment(x1) == rat(Rational(0)));  // strictly within [-5, 5]

  const std::size_t variables_before = tableau.variable_count();
  assert(!apply_gomory_cuts(tableau, {x_b}, 10));
  assert(tableau.variable_count() == variables_before);
}

// A zero cut_limit must never even look for an eligible row -- the
// tableau is left completely untouched.
void zero_cut_limit_is_a_no_op() {
  LraTableau tableau;
  const LraVarId x1 = tableau.add_variable();
  const LraVarId x2 = tableau.add_variable();
  const LraVarId x_b = tableau.add_variable();
  tableau.add_row(x_b, {{x1, Rational(2)}, {x2, Rational(3)}});
  assert(!tableau.assert_lower(x1, rat(Rational(0)), Lit{1}).has_value());
  assert(!tableau.assert_lower(x2, rat(Rational(1, 2)), Lit{2}).has_value());
  assert(!tableau.check().has_value());

  const std::size_t variables_before = tableau.variable_count();
  assert(!apply_gomory_cuts(tableau, {x_b}, 0));
  assert(tableau.variable_count() == variables_before);
}

}  // namespace

int main() {
  all_lower_bound_cut_tightens_without_resolving();
  mixed_lower_and_upper_bound_cut_tightens_without_resolving();
  cut_alone_proves_unsat();
  row_with_a_term_not_at_a_bound_is_skipped();
  zero_cut_limit_is_a_no_op();
  return 0;
}
