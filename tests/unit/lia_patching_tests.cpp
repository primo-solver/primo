#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <vector>

#include "sat/lit.hpp"
#include "theory/arithmetic/lia_integer_values.hpp"
#include "theory/arithmetic/lia_patching.hpp"
#include "theory/arithmetic/lra_tableau.hpp"
#include "util/rational.hpp"

using primo::sat::Lit;
using primo::theory::arithmetic::DeltaRational;
using primo::theory::arithmetic::is_integer_valued;
using primo::theory::arithmetic::LraTableau;
using primo::theory::arithmetic::LraVarId;
using primo::theory::arithmetic::patch_integer_variables;
using primo::util::Rational;

namespace {

DeltaRational rat(long value) { return DeltaRational::from_rational(Rational(value)); }
DeltaRational rat(const Rational& value) { return DeltaRational::from_rational(value); }

// The claim the whole stage rests on: a move confined to a freedom interval
// keeps every basic variable inside its bounds. Asserted directly after every
// patch rather than trusted, since a violation here is exactly what would make
// branch_and_bound report Sat on an infeasible assignment.
void assert_feasible(LraTableau& tableau) {
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) {
    const DeltaRational& value = tableau.assignment(var);
    if (const auto lower = tableau.lower_bound(var); lower.has_value()) {
      assert(value >= lower->value);
    }
    if (const auto upper = tableau.upper_bound(var); upper.has_value()) {
      assert(value <= upper->value);
    }
  }
  // And no repair should be needed at all -- check() must be a no-op.
  assert(!tableau.check().has_value());
}

// Bjorner & Nachmanson Sect. 4.1, Example 3, built exactly as the paper states
// it: y - x/2 = 0, z - x/3 = 0, with x in [3,10], y in [-3,4], z in [-5,12].
// x starts at its lower bound 3, which leaves y = 3/2 fractional. The paper's
// answer is to move x to 6 inside its freedom interval, making y = 3 and
// z = 2, both integral and both within bounds.
void paper_example_3_patches_x_to_an_integer_point() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId z = tableau.add_variable();
  tableau.add_row(y, {{x, Rational(1, 2)}});
  tableau.add_row(z, {{x, Rational(1, 3)}});
  assert(!tableau.assert_lower(x, rat(3), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(10), Lit{2}).has_value());
  assert(!tableau.assert_lower(y, rat(-3), Lit{3}).has_value());
  assert(!tableau.assert_upper(y, rat(4), Lit{4}).has_value());
  assert(!tableau.assert_lower(z, rat(-5), Lit{5}).has_value());
  assert(!tableau.assert_upper(z, rat(12), Lit{6}).has_value());
  assert(!tableau.check().has_value());

  // The paper's starting state: x at its bound, y fractional.
  assert(tableau.assignment(x) == rat(3));
  assert(tableau.assignment(y) == rat(Rational(3, 2)));
  assert(!is_integer_valued(tableau.assignment(y)));

  const std::size_t patched = patch_integer_variables(tableau, {x, y, z}, 100);
  assert(patched > 0);
  assert(is_integer_valued(tableau.assignment(x)));
  assert(is_integer_valued(tableau.assignment(y)));
  assert(is_integer_valued(tableau.assignment(z)));
  assert_feasible(tableau);
}

// The case the paper does not have to describe, because in its setting
// nonbasic variables are always at their bounds and therefore already
// integral. Primo's scale-canonicalization stores a fractional bound on an
// Int-sorted variable, so a nonbasic Int variable can sit at 5/3, and the fix
// is to move it to an integer in range with no row reasoning at all.
void fractional_nonbasic_is_rounded_in_place() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  assert(!tableau.assert_lower(x, rat(Rational(5, 3)), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(10), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  assert(!is_integer_valued(tableau.assignment(x)));

  assert(patch_integer_variables(tableau, {x}, 100) == 1);
  assert(tableau.assignment(x) == rat(2));
  assert_feasible(tableau);
}

// A variable pinned strictly between two consecutive integers cannot be
// patched at all: the freedom interval holds no integer. Patching must report
// no progress and leave the assignment exactly as it found it, so the
// refutation stages see an unchanged tableau.
void unpatchable_variable_is_left_untouched() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  assert(!tableau.assert_lower(x, rat(Rational(1, 3)), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(Rational(2, 3)), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  const DeltaRational before = tableau.assignment(x);

  assert(patch_integer_variables(tableau, {x}, 100) == 0);
  assert(tableau.assignment(x) == before);
  assert_feasible(tableau);
}

// A fixed variable has an empty freedom interval and must simply be skipped,
// not moved off its fixed value. 3x = 2 pins the slack, leaving x = 2/3 with
// nowhere to go.
void fixed_variable_is_not_moved() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(3)}});
  assert(!tableau.assert_lower(s, rat(2), Lit{1}).has_value());
  assert(!tableau.assert_upper(s, rat(2), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  const DeltaRational s_before = tableau.assignment(s);

  assert(patch_integer_variables(tableau, {x}, 100) == 0);
  assert(tableau.assignment(s) == s_before);
  assert_feasible(tableau);
}

// Delta-awareness. A strict lower bound stores 3 + eps, so the smallest
// integer the variable may take is 4, not 3 -- taking delta_floor's rational
// part alone would produce 3, which is below the bound.
void strict_bound_rounds_up_past_the_infinitesimal() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  assert(!tableau.assert_lower(x, DeltaRational{Rational(3), Rational(1)}, Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(8), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  assert(!is_integer_valued(tableau.assignment(x)));

  assert(patch_integer_variables(tableau, {x}, 100) == 1);
  assert(tableau.assignment(x) == rat(4));
  assert_feasible(tableau);
}

// A budget of zero is the A/B control and must be a complete no-op.
void zero_patch_limit_is_a_no_op() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  assert(!tableau.assert_lower(x, rat(Rational(5, 3)), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(10), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  const DeltaRational before = tableau.assignment(x);

  assert(patch_integer_variables(tableau, {x}, 0) == 0);
  assert(tableau.assignment(x) == before);
}

// An already-integral assignment gives patching nothing to do.
void already_integral_assignment_reports_no_progress() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  assert(!tableau.assert_lower(x, rat(2), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(9), Lit{2}).has_value());
  assert(!tableau.check().has_value());

  assert(patch_integer_variables(tableau, {x}, 100) == 0);
  assert(tableau.assignment(x) == rat(2));
  assert_feasible(tableau);
}

// Only strictly improving moves are taken, so a sweep with nothing to gain
// must leave the assignment byte-for-byte as it found it. That matters
// specifically because set_nonbasic_value is not trailed: there is no undo, so
// "never take a move that does not help" is the whole safety argument. Here x
// is pinned strictly between 0 and 1 and cannot be integralized at all.
void a_sweep_with_no_improving_move_changes_nothing() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}, {y, Rational(1)}});
  assert(!tableau.assert_lower(x, rat(Rational(1, 3)), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(Rational(2, 3)), Lit{2}).has_value());
  assert(!tableau.assert_lower(y, rat(0), Lit{3}).has_value());
  assert(!tableau.assert_upper(y, rat(5), Lit{4}).has_value());
  assert(!tableau.check().has_value());

  std::vector<DeltaRational> before;
  before.reserve(tableau.variable_count());
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) {
    before.push_back(tableau.assignment(var));
  }

  assert(patch_integer_variables(tableau, {x, y}, 100) == 0);
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) {
    assert(tableau.assignment(var) == before[var]);
  }
  assert_feasible(tableau);
}

}  // namespace

int main() {
  paper_example_3_patches_x_to_an_integer_point();
  fractional_nonbasic_is_rounded_in_place();
  unpatchable_variable_is_left_untouched();
  fixed_variable_is_not_moved();
  strict_bound_rounds_up_past_the_infinitesimal();
  zero_patch_limit_is_a_no_op();
  already_integral_assignment_reports_no_progress();
  a_sweep_with_no_improving_move_changes_nothing();
  return 0;
}
