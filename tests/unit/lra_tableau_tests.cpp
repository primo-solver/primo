#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <vector>

#include "sat/lit.hpp"
#include "theory/arithmetic/lra_tableau.hpp"
#include "util/rational.hpp"

using primo::sat::Lit;
using primo::theory::arithmetic::DeltaRational;
using primo::theory::arithmetic::LraTableau;
using primo::theory::arithmetic::LraVarId;
using primo::util::Rational;

namespace {

DeltaRational rat(long value) { return DeltaRational::from_rational(Rational(value)); }

bool has_reason(const std::vector<Lit>& reasons, Lit lit) {
  return std::find_if(reasons.begin(), reasons.end(),
                      [&](Lit r) { return r.value == lit.value; }) != reasons.end();
}

void direct_bound_conflict_on_a_single_variable() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();

  const Lit upper_reason{1};
  const auto ok = tableau.assert_upper(x, rat(5), upper_reason);
  assert(!ok.has_value());

  const Lit lower_reason{2};
  const auto conflict = tableau.assert_lower(x, rat(10), lower_reason);
  assert(conflict.has_value());
  assert(conflict->size() == 2);
  assert(has_reason(*conflict, upper_reason));
  assert(has_reason(*conflict, lower_reason));
}

void weaker_bound_is_a_silent_no_op() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  assert(!tableau.assert_upper(x, rat(5), Lit{1}).has_value());
  // A weaker upper bound (10 >= 5) must not overwrite the tighter one, so
  // a later lower bound of 7 should still conflict against the original 5.
  assert(!tableau.assert_upper(x, rat(10), Lit{2}).has_value());
  const auto conflict = tableau.assert_lower(x, rat(7), Lit{3});
  assert(conflict.has_value());
  assert(has_reason(*conflict, Lit{1}));
}

void satisfiable_system_via_a_slack_row() {
  // s = x + y, x >= 1, y >= 1, s <= 5. Feasible: x=y=1, s=2.
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}, {y, Rational(1)}});

  assert(!tableau.assert_upper(s, rat(5), Lit{1}).has_value());
  assert(!tableau.assert_lower(x, rat(1), Lit{2}).has_value());
  assert(!tableau.assert_lower(y, rat(1), Lit{3}).has_value());

  const auto result = tableau.check();
  assert(!result.has_value());
  assert(tableau.assignment(x) + tableau.assignment(y) == tableau.assignment(s));
  assert(tableau.assignment(s) <= rat(5));
  assert(tableau.assignment(x) >= rat(1));
  assert(tableau.assignment(y) >= rat(1));
}

void infeasible_system_requires_a_pivot_to_discover() {
  // s1 = x + y <= 1, s2 = x + y >= 3, x >= 0, y >= 0.
  // s1/s2 both start at assignment 0 (x=y=0), so the conflict between them
  // is only discovered once check() propagates a repair pivot -- this is
  // the scenario that actually exercises Pivot, not just Assert-Bound's
  // immediate-conflict shortcut.
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s1 = tableau.add_variable();
  const LraVarId s2 = tableau.add_variable();
  tableau.add_row(s1, {{x, Rational(1)}, {y, Rational(1)}});
  tableau.add_row(s2, {{x, Rational(1)}, {y, Rational(1)}});

  const Lit upper_reason{1};
  const Lit lower_reason{2};
  assert(!tableau.assert_upper(s1, rat(1), upper_reason).has_value());
  assert(!tableau.assert_lower(s2, rat(3), lower_reason).has_value());
  assert(!tableau.assert_lower(x, rat(0), Lit{3}).has_value());
  assert(!tableau.assert_lower(y, rat(0), Lit{4}).has_value());

  const auto conflict = tableau.check();
  assert(conflict.has_value());
  // The x>=0/y>=0 bounds are never needed: s1<=1 and s2<=3 with s1==s2==x+y
  // are directly contradictory regardless of x, y's own bounds -- a valid
  // solver should find this minimal 2-literal certificate, not drag in the
  // unrelated bounds.
  assert(conflict->size() == 2);
  assert(has_reason(*conflict, upper_reason));
  assert(has_reason(*conflict, lower_reason));
}

void strict_inequality_conflicts_with_touching_nonstrict_bound() {
  // s = x (a slack standing in for the atom "x < 5"), x >= 5.
  // Rational parts are equal (5 == 5) -- only the infinitesimal makes this
  // a genuine conflict, exercising the DeltaRational ordering directly.
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}});

  const DeltaRational strict_upper{Rational(5), Rational(-1)};
  const Lit upper_reason{1};
  const Lit lower_reason{2};
  assert(!tableau.assert_upper(s, strict_upper, upper_reason).has_value());
  assert(!tableau.assert_lower(x, rat(5), lower_reason).has_value());

  const auto conflict = tableau.check();
  assert(conflict.has_value());
  assert(conflict->size() == 2);
  assert(has_reason(*conflict, upper_reason));
  assert(has_reason(*conflict, lower_reason));
}

void strict_inequality_is_satisfiable_when_not_touching() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}});

  const DeltaRational strict_upper{Rational(5), Rational(-1)};
  assert(!tableau.assert_upper(s, strict_upper, Lit{1}).has_value());
  assert(!tableau.assert_lower(x, rat(4), Lit{2}).has_value());

  const auto result = tableau.check();
  assert(!result.has_value());
  assert(tableau.assignment(s) <= strict_upper);
}

void empty_row_from_a_trivially_true_atom_is_handled() {
  // (<= (+ x y) (+ y x)): two syntactically different but semantically
  // identical polynomials normalize to "0 <= 0" -- an empty row.
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  (void)y;
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {});
  assert(tableau.assignment(s) == rat(0));
  assert(!tableau.assert_upper(s, rat(0), Lit{1}).has_value());
  assert(!tableau.check().has_value());

  // But asserting an unsatisfiable bound on that same trivial 0 is a
  // self-contained (single-reason) conflict.
  const auto conflict = tableau.assert_upper(s, rat(-1), Lit{2});
  assert(!conflict.has_value());  // -1 < 0 tightens the bound, no direct clash yet
  const auto check_conflict = tableau.check();
  assert(check_conflict.has_value());
  assert(check_conflict->size() == 1);
  assert(has_reason(*check_conflict, Lit{2}));
  (void)x;
}

}  // namespace

int main() {
  direct_bound_conflict_on_a_single_variable();
  weaker_bound_is_a_silent_no_op();
  satisfiable_system_via_a_slack_row();
  infeasible_system_requires_a_pivot_to_discover();
  strict_inequality_conflicts_with_touching_nonstrict_bound();
  strict_inequality_is_satisfiable_when_not_touching();
  empty_row_from_a_trivially_true_atom_is_handled();
  return 0;
}
