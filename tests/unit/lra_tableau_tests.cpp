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

void push_pop_restores_a_bound() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();

  assert(!tableau.assert_upper(x, rat(5), Lit{1}).has_value());
  const std::size_t checkpoint = tableau.push();
  // Tighten the bound inside the pushed level.
  assert(!tableau.assert_upper(x, rat(2), Lit{2}).has_value());
  // Asserting a lower bound of 3 conflicts with the *tightened* bound...
  const auto conflict_while_pushed = tableau.assert_lower(x, rat(3), Lit{3});
  assert(conflict_while_pushed.has_value());
  assert(has_reason(*conflict_while_pushed, Lit{2}));

  tableau.pop(checkpoint);
  // ...but not with the original bound of 5, once popped: 3 <= 5 is fine.
  const auto after_pop = tableau.assert_lower(x, rat(3), Lit{3});
  assert(!after_pop.has_value());
}

void push_pop_restores_state_across_a_pivot() {
  // Same scenario as infeasible_system_requires_a_pivot_to_discover, but
  // the conflicting bound (and the pivot check() performs to discover it)
  // happens *inside* a pushed level. After pop, the tableau must be back
  // to a state indistinguishable from having never asserted that bound or
  // pivoted at all -- verified by checking it's SAT again, and that
  // reasserting the same bound reproduces the identical conflict a fresh
  // tableau would find.
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s1 = tableau.add_variable();
  const LraVarId s2 = tableau.add_variable();
  tableau.add_row(s1, {{x, Rational(1)}, {y, Rational(1)}});
  tableau.add_row(s2, {{x, Rational(1)}, {y, Rational(1)}});

  const Lit upper_reason{1};
  assert(!tableau.assert_upper(s1, rat(1), upper_reason).has_value());
  assert(!tableau.assert_lower(x, rat(0), Lit{3}).has_value());
  assert(!tableau.assert_lower(y, rat(0), Lit{4}).has_value());
  assert(!tableau.check().has_value());  // feasible so far

  const std::size_t checkpoint = tableau.push();
  const Lit lower_reason{2};
  assert(!tableau.assert_lower(s2, rat(3), lower_reason).has_value());
  const auto conflict = tableau.check();  // forces a repair pivot, then finds the conflict
  assert(conflict.has_value());
  assert(conflict->size() == 2);
  assert(has_reason(*conflict, upper_reason));
  assert(has_reason(*conflict, lower_reason));

  tableau.pop(checkpoint);
  assert(!tableau.check().has_value());  // feasible again

  // Redo the exact same assertion; must reproduce the identical conflict.
  const auto redone_conflict = tableau.assert_lower(s2, rat(3), lower_reason);
  const auto redone_check_conflict = redone_conflict.has_value() ? redone_conflict : tableau.check();
  assert(redone_check_conflict.has_value());
  assert(redone_check_conflict->size() == 2);
  assert(has_reason(*redone_check_conflict, upper_reason));
  assert(has_reason(*redone_check_conflict, lower_reason));
}

void push_pop_restores_propagated_basic_assignment() {
  // s = x + y; asserting a lower bound on the *non-basic* x propagates
  // into s's (basic) assignment via update()'s propagation loop -- verify
  // that propagated change is undone on pop too, not just x's own bound.
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}, {y, Rational(1)}});
  assert(tableau.assignment(s) == rat(0));

  const std::size_t checkpoint = tableau.push();
  assert(!tableau.assert_lower(x, rat(5), Lit{1}).has_value());
  assert(tableau.assignment(x) == rat(5));
  assert(tableau.assignment(s) == rat(5));  // propagated

  tableau.pop(checkpoint);
  assert(tableau.assignment(x) == rat(0));
  assert(tableau.assignment(s) == rat(0));
}

void nested_push_pop_levels() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();

  assert(!tableau.assert_lower(x, rat(0), Lit{1}).has_value());
  const std::size_t level1 = tableau.push();
  assert(!tableau.assert_upper(x, rat(10), Lit{2}).has_value());
  const std::size_t level2 = tableau.push();
  assert(!tableau.assert_upper(x, rat(3), Lit{3}).has_value());  // tighter still

  // A lower bound of 4 conflicts with the innermost (3) but not level1's (10).
  assert(tableau.assert_lower(x, rat(4), Lit{4}).has_value());
  tableau.pop(level2);
  assert(!tableau.assert_lower(x, rat(4), Lit{4}).has_value());  // 4 <= 10, fine now

  tableau.pop(level1);
  // Back to the base level: only the original lower bound of 0 remains, no
  // upper bound at all, so even a huge lower bound is unconstrained above.
  assert(!tableau.assert_lower(x, rat(1000), Lit{5}).has_value());
}

void bound_accessors_report_nullopt_before_any_assertion() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  assert(!tableau.upper_bound(x).has_value());
  assert(!tableau.lower_bound(x).has_value());
}

void bound_accessors_report_value_and_reason() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const Lit upper_reason{1};
  const Lit lower_reason{2};
  assert(!tableau.assert_upper(x, rat(5), upper_reason).has_value());
  assert(!tableau.assert_lower(x, rat(1), lower_reason).has_value());

  const auto upper = tableau.upper_bound(x);
  assert(upper.has_value());
  assert(upper->value == rat(5));
  assert(upper->reason.value == upper_reason.value);

  const auto lower = tableau.lower_bound(x);
  assert(lower.has_value());
  assert(lower->value == rat(1));
  assert(lower->reason.value == lower_reason.value);

  // A weaker bound is a silent no-op (per assert_upper's own contract), so
  // the accessor must still report the original, tighter bound/reason.
  assert(!tableau.assert_upper(x, rat(10), Lit{3}).has_value());
  assert(tableau.upper_bound(x)->reason.value == upper_reason.value);
}

// Milestone 4: PivotingRule::Dantzig must reach the same answers as the
// default Bland's-rule selection on scenarios that actually require a
// pivot -- the two rules only differ in *which* variable each loop picks,
// never in correctness. Re-uses satisfiable_system_via_a_slack_row's and
// infeasible_system_requires_a_pivot_to_discover's exact scenarios above.
void satisfiable_system_via_a_slack_row_under_dantzig() {
  LraTableau tableau(LraTableau::PivotingRule::Dantzig);
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
  assert(tableau.bland_fallback_count() == 0);
}

void infeasible_system_requires_a_pivot_to_discover_under_dantzig() {
  LraTableau tableau(LraTableau::PivotingRule::Dantzig);
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
  assert(conflict->size() == 2);
  assert(has_reason(*conflict, upper_reason));
  assert(has_reason(*conflict, lower_reason));
  assert(tableau.bland_fallback_count() == 0);
}

// A scenario with rows of differing coefficient magnitude and differing
// violation sizes, so Dantzig's "largest coefficient"/"largest violation"
// selection actually has more than one candidate to choose between (not
// just a single trivial pivot) -- still only checked for the same
// end-to-end correctness as Bland, per the class comment's scope (the
// specific pivot sequence chosen isn't part of the contract).
void multi_pivot_system_with_varied_coefficients_under_dantzig() {
  // s1 = 2x + y, s2 = x + 3y, x >= 0, y >= 0, s1 <= 4, s2 <= 9.
  // Feasible, e.g. x=0, y=3: s1=3<=4, s2=9<=9.
  LraTableau tableau(LraTableau::PivotingRule::Dantzig);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s1 = tableau.add_variable();
  const LraVarId s2 = tableau.add_variable();
  tableau.add_row(s1, {{x, Rational(2)}, {y, Rational(1)}});
  tableau.add_row(s2, {{x, Rational(1)}, {y, Rational(3)}});

  assert(!tableau.assert_lower(x, rat(0), Lit{1}).has_value());
  assert(!tableau.assert_lower(y, rat(0), Lit{2}).has_value());
  assert(!tableau.assert_upper(s1, rat(4), Lit{3}).has_value());
  assert(!tableau.assert_upper(s2, rat(9), Lit{4}).has_value());
  // Force both rows out of their upper bounds via a large lower bound
  // on y, requiring the Check/Pivot loop to actually run.
  assert(!tableau.assert_lower(y, rat(20), Lit{5}).has_value());

  const auto conflict = tableau.check();
  assert(conflict.has_value());
  assert(tableau.bland_fallback_count() == 0);
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
  push_pop_restores_a_bound();
  push_pop_restores_state_across_a_pivot();
  push_pop_restores_propagated_basic_assignment();
  nested_push_pop_levels();
  bound_accessors_report_nullopt_before_any_assertion();
  bound_accessors_report_value_and_reason();
  satisfiable_system_via_a_slack_row_under_dantzig();
  infeasible_system_requires_a_pivot_to_discover_under_dantzig();
  multi_pivot_system_with_varied_coefficients_under_dantzig();
  return 0;
}
