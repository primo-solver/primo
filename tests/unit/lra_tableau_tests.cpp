#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <utility>
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

// The two invariants that make bounds-only backtracking sound (see
// LraTableau's class comment, and SRI-CSL-06-01 Section 3.2.4): the
// assignment still solves every row equation, and every non-basic variable
// still sits within its own bounds. pop() is required to preserve these --
// it is *not* required to restore the assignment or the basis, so tests
// check these rather than field-by-field equality with the pre-push state.
void assert_tableau_invariants(const LraTableau& tableau) {
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) {
    if (tableau.is_basic(var)) {
      DeltaRational sum = rat(0);
      for (const auto& [term_var, coeff, term_slot] : tableau.row(var)) {
        sum = sum + tableau.assignment(term_var).scaled(coeff);
      }
      assert(sum == tableau.assignment(var));
      continue;
    }
    const auto upper = tableau.upper_bound(var);
    const auto lower = tableau.lower_bound(var);
    assert(!upper.has_value() || tableau.assignment(var) <= upper->value);
    assert(!lower.has_value() || tableau.assignment(var) >= lower->value);
  }
}

// Applies update(x_j, v) the way LraTableau::update does -- every basic
// variable in x_j's column moves by a_ij * (v - beta(x_j)) -- and reports
// whether the resulting assignment is a genuine model: every variable,
// BASIC ONES INCLUDED, inside its bounds.
//
// Simulated rather than applied because freedom_interval's whole contract is
// about what *would* happen, and because the diversifier deliberately works
// on a scratch assignment (update() stays private). Checking basic variables
// is the point: assert_tableau_invariants above deliberately does not, since
// check() is allowed to leave them violated mid-repair.
bool update_would_stay_feasible(const LraTableau& tableau, LraVarId nonbasic,
                                const DeltaRational& value) {
  const auto within = [&](LraVarId var, const DeltaRational& candidate) {
    const auto upper = tableau.upper_bound(var);
    const auto lower = tableau.lower_bound(var);
    return (!upper.has_value() || candidate <= upper->value) &&
           (!lower.has_value() || candidate >= lower->value);
  };
  if (!within(nonbasic, value)) {
    return false;
  }
  const DeltaRational shift = value - tableau.assignment(nonbasic);
  for (LraVarId basic = 0; basic < tableau.variable_count(); ++basic) {
    if (!tableau.is_basic(basic)) {
      continue;
    }
    DeltaRational moved = tableau.assignment(basic);
    for (const auto& [term_var, coeff, term_slot] : tableau.row(basic)) {
      if (term_var == nonbasic) {
        moved = moved + shift.scaled(coeff);
      }
    }
    if (!within(basic, moved)) {
      return false;
    }
  }
  return true;
}

DeltaRational midpoint(const DeltaRational& lhs, const DeltaRational& rhs) {
  return (lhs + rhs).scaled(Rational(1) / Rational(2));
}

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
  const auto redone_check_conflict =
      redone_conflict.has_value() ? redone_conflict : tableau.check();
  assert(redone_check_conflict.has_value());
  assert(redone_check_conflict->size() == 2);
  assert(has_reason(*redone_check_conflict, upper_reason));
  assert(has_reason(*redone_check_conflict, lower_reason));
}

bool same_reason_set(const std::optional<std::vector<Lit>>& a,
                     const std::optional<std::vector<Lit>>& b) {
  if (a.has_value() != b.has_value()) {
    return false;
  }
  if (!a.has_value()) {
    return true;
  }
  if (a->size() != b->size()) {
    return false;
  }
  return std::all_of(a->begin(), a->end(), [&](Lit lit) { return has_reason(*b, lit); });
}

void push_pop_rebuilds_violated_vars_across_multiple_simultaneous_violations() {
  // Targets violated_vars_'s own trickiest invariant (see its comment in
  // lra_tableau.hpp): a single bound assertion that simultaneously
  // violates *two* different basic rows (x appears in both s1 and s2),
  // forcing check()'s pivot loop to work through more than one entry in
  // the set, followed by a pop() that must reconstruct that set correctly
  // from scratch rather than by patching individual undo cases. The
  // oracle is push_pop_restores_state_across_a_pivot's own pattern:
  // whatever check() finds the first time (inside the pushed level) must
  // be bit-for-bit reproducible by redoing the identical assertion after
  // popping -- this doesn't require hand-predicting the exact SAT/
  // conflict outcome, only that push/pop is a true no-op on it.
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s1 = tableau.add_variable();
  const LraVarId s2 = tableau.add_variable();
  tableau.add_row(s1, {{x, Rational(1)}, {y, Rational(1)}});
  tableau.add_row(s2, {{x, Rational(1)}, {y, Rational(-1)}});

  assert(!tableau.assert_lower(x, rat(0), Lit{1}).has_value());
  assert(!tableau.assert_upper(s1, rat(1), Lit{2}).has_value());
  assert(!tableau.assert_upper(s2, rat(1), Lit{3}).has_value());
  assert(!tableau.check().has_value());  // feasible: x=y=0, s1=s2=0

  const std::size_t checkpoint = tableau.push();
  const Lit trigger_reason{4};
  // x jumping to 10 (y still 0) makes s1=x+y=10>1 *and* s2=x-y=10>1
  // simultaneously -- both rows enter violated_vars_ from this one call.
  assert(!tableau.assert_lower(x, rat(10), trigger_reason).has_value());
  const auto result_inside_push = tableau.check();

  tableau.pop(checkpoint);
  assert(!tableau.check().has_value());  // back to the pre-push feasible state

  assert(!tableau.assert_lower(x, rat(10), trigger_reason).has_value());
  const auto result_after_pop_and_redo = tableau.check();

  assert(same_reason_set(result_inside_push, result_after_pop_and_redo));
}

void pop_restores_bounds_and_keeps_the_assignment_consistent() {
  // s = x + y; asserting a lower bound on the *non-basic* x propagates into
  // s's (basic) assignment via update()'s propagation loop.
  //
  // pop() deliberately does NOT put either assignment back. Bounds-only
  // backtracking is the paper's own scheme (SRI-CSL-06-01 Section 3.2.4),
  // and this test pins the two invariants that make it sound, which are
  // what actually matters -- not the stronger "every field is byte-identical
  // to before the push", which this test used to assert and which the paper
  // says is unnecessary work.
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

  // (1) The bound itself is gone -- that is the whole job of pop(). An
  // upper bound of 2 would have conflicted with x >= 5 before the pop.
  assert(!tableau.assert_upper(x, rat(2), Lit{2}).has_value());

  // (2) The assignment still satisfies every row equation, which is why it
  // does not need restoring: it solved Ax = 0 before the pop and the rows
  // did not change.
  assert_tableau_invariants(tableau);
  assert(tableau.assignment(s) == tableau.assignment(x) + tableau.assignment(y));

  // (3) And the search can still make progress from here.
  assert(!tableau.check().has_value());
  assert_tableau_invariants(tableau);
  assert(tableau.assignment(s) == tableau.assignment(x) + tableau.assignment(y));
  assert(tableau.assignment(x) <= rat(2));
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

// PivotingRule::Sparse (heuristics-note item H3, Monniaux CAV 2009 p.8).
// Among the entering candidates that all repair the violation equally, take
// the one occurring in the fewest rows -- those are exactly the rows pivot()
// rewrites.

void sparse_picks_the_column_occurring_in_fewest_rows() {
  // s0 = x + y, and y additionally occurs in s1 and s2 while x occurs
  // nowhere else. Repairing s0 admits either x or y; Bland would take the
  // smaller index (x is 0, y is 1 -- so Bland happens to agree here, which is
  // why the coefficients below make Dantzig disagree), Dantzig would take the
  // larger |coefficient| (y, at 5), and Sparse must take x, which occurs in
  // one row rather than three.
  const auto build = [](LraTableau::PivotingRule rule) {
    LraTableau tableau(rule);
    const LraVarId x = tableau.add_variable();
    const LraVarId y = tableau.add_variable();
    const LraVarId s0 = tableau.add_variable();
    const LraVarId s1 = tableau.add_variable();
    const LraVarId s2 = tableau.add_variable();
    tableau.add_row(s0, {{x, Rational(1)}, {y, Rational(5)}});
    tableau.add_row(s1, {{y, Rational(1)}});
    tableau.add_row(s2, {{y, Rational(1)}});
    // Drive s0 below a lower bound so check() must pivot to repair it.
    assert(!tableau.assert_lower(s0, rat(10), Lit{1}).has_value());
    assert(!tableau.check().has_value());
    return tableau;
  };

  const LraTableau sparse = build(LraTableau::PivotingRule::Sparse);
  const LraTableau dantzig = build(LraTableau::PivotingRule::Dantzig);
  // Sparse brings in x (one occurrence); Dantzig brings in y (coefficient 5).
  assert(sparse.is_basic(0));
  assert(!sparse.is_basic(1));
  assert(!dantzig.is_basic(0));
  assert(dantzig.is_basic(1));
  // Whichever entered, the repaired system must satisfy the bound -- the
  // choice is a cost decision, never a correctness one.
  assert(sparse.assignment(2) >= rat(10));
  assert(dantzig.assignment(2) >= rat(10));
}

void sparse_prefers_a_unit_coefficient_when_occurrences_tie() {
  // Both x and y occur in exactly one row, so the sparsity score ties and the
  // tie-break decides: x's coefficient is 3, y's is 1. Pivoting on +/-1
  // introduces no new denominator, which is what keeps coefficients inside
  // util::Rational's machine-word fast path.
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(3)}, {y, Rational(1)}});

  assert(!tableau.assert_lower(s, rat(6), Lit{1}).has_value());
  assert(!tableau.check().has_value());
  assert(!tableau.is_basic(x));
  assert(tableau.is_basic(y));
  assert(tableau.assignment(s) >= rat(6));
}

void sparse_bounded_pricing_still_finds_the_only_eligible_candidate() {
  // The candidate scan is capped, but the cap may only bound how many
  // *eligible* candidates get scored -- never how much of the row is read
  // before concluding there are none, because "none" is what check() turns
  // into a conflict. Here the row is far wider than the cap and every term
  // except the last is pinned at a bound it cannot move away from, so the one
  // eligible candidate sits past where a naive cap would stop looking. The
  // system is satisfiable, so reporting a conflict would be unsound.
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  constexpr int kWidth = 64;  // Comfortably past kSparsePricingCandidateLimit.
  std::vector<std::pair<LraVarId, Rational>> row;
  std::vector<LraVarId> pinned;
  for (int i = 0; i < kWidth; ++i) {
    const LraVarId v = tableau.add_variable();
    pinned.push_back(v);
    row.emplace_back(v, Rational(1));
  }
  const LraVarId free_var = tableau.add_variable();
  row.emplace_back(free_var, Rational(1));
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, row);

  // Pin every term but the last to exactly 0, so none of them can increase.
  for (int i = 0; i < kWidth; ++i) {
    assert(
        !tableau.assert_upper(pinned[static_cast<std::size_t>(i)], rat(0), Lit{i + 1}).has_value());
  }
  // s >= 1 is repairable only by raising free_var.
  assert(!tableau.assert_lower(s, rat(1), Lit{1000}).has_value());

  assert(!tableau.check().has_value());  // Must pivot, not report a conflict.
  assert(tableau.is_basic(free_var));
  assert(tableau.assignment(s) >= rat(1));
}

// The leaving rule from Griggio's thesis (Section 2.5.3, page 53): pick the
// LEAST-violated basic variable, tie-broken by the shorter row. It is a cost
// lever like the others, so it must not change any verdict -- and it must
// actually select differently from the two policies Primo already had, or it
// is not the rule it claims to be.
void least_violated_leaving_matches_bland_on_multi_pivot_scenarios() {
  const auto run = [](LraTableau::PivotingRule rule, bool sparse_leaving, bool least_violated) {
    LraTableau tableau(rule);
    tableau.set_sparse_leaving(sparse_leaving);
    tableau.set_least_violated_leaving(least_violated);
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
    assert(!tableau.assert_lower(y, rat(20), Lit{5}).has_value());
    return tableau.check().has_value();
  };
  const bool bland = run(LraTableau::PivotingRule::Bland, false, false);
  assert(run(LraTableau::PivotingRule::Sparse, false, true) == bland);
  assert(run(LraTableau::PivotingRule::Sparse, true, true) == bland);
}

// Pins that the rule actually selects differently, rather than merely being
// wired up. Two violated basic variables whose violations differ in
// magnitude: the least-violated rule repairs the smaller one first while the
// default (Dantzig-style) repairs the larger, and on this shape that changes
// the resulting pivot sequence. Observed through pivot_count(), which is a
// real consequence of selection rather than a restatement of the branch.
void least_violated_leaving_selects_differently_from_most_violated() {
  const auto pivots = [](bool least_violated) {
    LraTableau tableau(LraTableau::PivotingRule::Sparse);
    tableau.set_least_violated_leaving(least_violated);
    const LraVarId x = tableau.add_variable();
    const LraVarId y = tableau.add_variable();
    const LraVarId s1 = tableau.add_variable();
    const LraVarId s2 = tableau.add_variable();
    // Two rows over a shared variable, so repairing one disturbs the other
    // and the order in which they are repaired is observable.
    tableau.add_row(s1, {{x, Rational(1)}, {y, Rational(1)}});
    tableau.add_row(s2, {{x, Rational(1)}, {y, Rational(-1)}});
    assert(!tableau.assert_lower(x, rat(0), Lit{1}).has_value());
    assert(!tableau.assert_lower(y, rat(0), Lit{2}).has_value());
    // s1 is driven far below its bound, s2 only slightly.
    assert(!tableau.assert_lower(s1, rat(50), Lit{3}).has_value());
    assert(!tableau.assert_lower(s2, rat(1), Lit{4}).has_value());
    tableau.check();
    return tableau.pivot_count();
  };
  // Both must terminate; the point is that the selection differs, which on
  // this shape shows up as a different amount of pivoting.
  const std::size_t most = pivots(false);
  const std::size_t least = pivots(true);
  assert(most > 0 && least > 0);
  assert(most != least);
}

// The numeric Tuning knobs are cost levers only: they choose among candidates
// that all repair the violation equally, and they move when Bland's rule takes
// over. Neither may change a verdict. This is the property a tuning campaign
// relies on -- without it, searching the space would be a correctness risk
// rather than a performance one.
void tuning_knobs_never_change_a_verdict() {
  const auto run = [](LraTableau::Tuning tuning) {
    LraTableau tableau(LraTableau::PivotingRule::Sparse);
    tableau.set_tuning(tuning);
    const LraVarId x = tableau.add_variable();
    const LraVarId y = tableau.add_variable();
    const LraVarId z = tableau.add_variable();
    const LraVarId s1 = tableau.add_variable();
    const LraVarId s2 = tableau.add_variable();
    tableau.add_row(s1, {{x, Rational(2)}, {y, Rational(1)}, {z, Rational(1)}});
    tableau.add_row(s2, {{x, Rational(1)}, {y, Rational(3)}, {z, Rational(-1)}});
    assert(!tableau.assert_lower(x, rat(0), Lit{1}).has_value());
    assert(!tableau.assert_lower(y, rat(0), Lit{2}).has_value());
    assert(!tableau.assert_lower(z, rat(0), Lit{3}).has_value());
    assert(!tableau.assert_upper(s1, rat(4), Lit{4}).has_value());
    assert(!tableau.assert_upper(s2, rat(9), Lit{5}).has_value());
    assert(!tableau.assert_lower(y, rat(20), Lit{6}).has_value());
    return tableau.check().has_value();
  };
  const bool expected = run(LraTableau::Tuning{});
  // 1 is the cheapest pricing (first eligible candidate), 0 the most thorough
  // (score every one); both ends and a mid value must agree.
  assert(run(LraTableau::Tuning{1, 4, 64}) == expected);
  assert(run(LraTableau::Tuning{2, 4, 64}) == expected);
  assert(run(LraTableau::Tuning{0, 4, 64}) == expected);
  // A zero threshold falls back to Bland selection almost immediately, which
  // is the slowest but most conservative end of that knob.
  assert(run(LraTableau::Tuning{16, 0, 0}) == expected);
  assert(run(LraTableau::Tuning{0, 0, 0}) == expected);
}

// Pins the fallback counter for the *default* rule. Until 2026-08-06 the
// increment was guarded by `rule_ == Dantzig`, so under Sparse the counter
// stayed at 0 even on calls that genuinely switched to Bland selection --
// which made :lra.bland-fallbacks unable to answer the only question it is
// consulted for, namely whether the threshold ever binds.
void bland_fallback_is_counted_under_the_sparse_rule() {
  const auto fallbacks = [](LraTableau::Tuning tuning) {
    LraTableau tableau(LraTableau::PivotingRule::Sparse);
    tableau.set_tuning(tuning);
    const LraVarId x = tableau.add_variable();
    const LraVarId y = tableau.add_variable();
    const LraVarId z = tableau.add_variable();
    const LraVarId s1 = tableau.add_variable();
    const LraVarId s2 = tableau.add_variable();
    const LraVarId s3 = tableau.add_variable();
    // A cyclic system: s1+s2+s3 is identically 0, so requiring each to be at
    // least 1 is infeasible -- and discovering that takes more than one pivot
    // round, which is what the threshold is compared against.
    tableau.add_row(s1, {{x, Rational(1)}, {y, Rational(-1)}});
    tableau.add_row(s2, {{y, Rational(1)}, {z, Rational(-1)}});
    tableau.add_row(s3, {{z, Rational(1)}, {x, Rational(-1)}});
    assert(!tableau.assert_lower(s1, rat(1), Lit{1}).has_value());
    assert(!tableau.assert_lower(s2, rat(1), Lit{2}).has_value());
    assert(!tableau.assert_lower(s3, rat(1), Lit{3}).has_value());
    assert(tableau.check().has_value());
    assert(tableau.pivot_count() > 1);
    return tableau.bland_fallback_count();
  };
  // A zero threshold forces the fallback on any call that pivots at all.
  assert(fallbacks(LraTableau::Tuning{16, 0, 0}) > 0);
  // The generous default must not trip it on a problem this small, so the
  // assertion above is testing the fallback and not merely the counter.
  assert(fallbacks(LraTableau::Tuning{}) == 0);
}

void sparse_matches_bland_on_multi_pivot_scenarios() {
  // Same shape as soi_matches_bland_on_multi_pivot_scenarios: the rule is a
  // pure cost lever, so it must agree with Bland on every verdict.
  const auto run = [](LraTableau::PivotingRule rule, bool sparse_leaving) {
    LraTableau tableau(rule);
    tableau.set_sparse_leaving(sparse_leaving);
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
    assert(!tableau.assert_lower(y, rat(20), Lit{5}).has_value());
    return tableau.check().has_value();
  };
  const bool bland = run(LraTableau::PivotingRule::Bland, false);
  assert(run(LraTableau::PivotingRule::Sparse, false) == bland);
  // The leaving-side knob is a separate opt-in and must not change verdicts
  // either.
  assert(run(LraTableau::PivotingRule::Sparse, true) == bland);
}

// PivotingRule::Soi (competitiveness-roadmap item 4, King/Barrett/Dutertre
// FMCAD 2013 "Simplex with Sum of Infeasibilities for SMT"). The paper's
// own Fig 6 worked example, reproduced exactly: T: x1 = 2*x2 - x3;
// 3<=x1<=7 (violated, a(x1)=1); x2<=3 (a(x2)=1); 1<=x3 (a(x3)=1). The
// paper hand-traces SOISELECT choosing entering=x2, delta=1, leaving=x1,
// reaching Vio(V)=0 in that single pivot -- verified here end-to-end via
// check()'s final state (is_basic/assignment), not by reaching into
// soi_select() directly, since it's private.
//
// Primo's tableau only ever moves a nonbasic variable's assignment away
// from 0 via a violated-bound update(), so to reproduce a(x2)=1 (which
// has no lower bound in the paper's own setup) a harmless x2>=1 lower
// bound is added here -- it doesn't change Flex(f) eligibility or the
// breakpoint search for x2 in this scenario (eligibility only checks the
// upper-bound side here, since f's coefficient for x2 is negative; see
// the test body), so it's a faithful reproduction, not a different
// scenario.
void soi_reproduces_paper_fig6_worked_example() {
  LraTableau tableau(LraTableau::PivotingRule::Soi);
  const LraVarId x2 = tableau.add_variable();
  const LraVarId x3 = tableau.add_variable();
  const LraVarId x1 = tableau.add_variable();
  tableau.add_row(x1, {{x2, Rational(2)}, {x3, Rational(-1)}});

  // Order matters: asserting x3's lower bound first, then x2's, drives
  // x1's assignment (propagated via update()) to exactly 1 = 2*1 - 1,
  // matching the paper's a(x1)=1 purely through ordinary bound-assertion
  // propagation, no private/manual state poking.
  assert(!tableau.assert_lower(x3, rat(1), Lit{1}).has_value());
  assert(!tableau.assert_lower(x2, rat(1), Lit{2}).has_value());  // see comment above
  assert(!tableau.assert_upper(x2, rat(3), Lit{3}).has_value());
  assert(tableau.assignment(x2) == rat(1));
  assert(tableau.assignment(x3) == rat(1));
  assert(tableau.assignment(x1) == rat(1));

  assert(!tableau.assert_lower(x1, rat(3), Lit{4}).has_value());
  assert(!tableau.assert_upper(x1, rat(7), Lit{5}).has_value());
  // x1 (basic) now violates its lower bound (1 < 3) -- exactly the
  // paper's starting point, Vio(V)=2.

  const auto result = tableau.check();
  assert(!result.has_value());  // satisfiable after exactly one pivot
  assert(!tableau.is_basic(x1));
  assert(tableau.is_basic(x2));
  assert(tableau.assignment(x1) == rat(3));  // reached its lower bound exactly
  assert(tableau.assignment(x2) == rat(2));  // 1 + delta(=1)
  assert(tableau.assignment(x3) == rat(1));  // unaffected by this pivot
  assert(tableau.soi_multi_row_conflict_count() == 0);
}

void soi_zero_breakpoints_moving_inward_do_not_change_the_slope() {
  // Section V-A's one-sided starting slope: both initially tight rows move
  // inward, so neither contributes to the violation objective. Mirror the
  // entering direction and row signs, including purely infinitesimal steps.
  for (const int direction : {-1, 1}) {
    for (const int row_sign : {-1, 1}) {
      for (const DeltaRational& target : {rat(1), DeltaRational{Rational(0), Rational(1)},
                                          DeltaRational{Rational(1), Rational(1)}}) {
        LraTableau tableau(LraTableau::PivotingRule::Soi);
        const LraVarId x = tableau.add_variable();
        const LraVarId y = tableau.add_variable();
        const LraVarId s = tableau.add_variable();
        const LraVarId r = tableau.add_variable();
        const LraVarId t = tableau.add_variable();
        tableau.add_row(s, {{x, Rational(direction)}, {y, Rational(1)}});
        tableau.add_row(r, {{x, Rational(row_sign)}, {y, Rational(-1)}});
        tableau.add_row(t, {{x, Rational(-row_sign)}, {y, Rational(-2)}});
        assert(!tableau.assert_lower(y, rat(0), Lit{1}));
        assert(!tableau.assert_upper(y, rat(0), Lit{2}));
        const auto checkpoint = tableau.push();

        for (int replay = 0; replay < 2; ++replay) {
          assert(!tableau.assert_lower(s, target, Lit{3}));
          if (direction == row_sign) {
            assert(!tableau.assert_lower(r, rat(0), Lit{4}));
            assert(!tableau.assert_upper(t, rat(0), Lit{5}));
          } else {
            assert(!tableau.assert_upper(r, rat(0), Lit{4}));
            assert(!tableau.assert_lower(t, rat(0), Lit{5}));
          }
          const auto before = tableau.pivot_count();
          assert(!tableau.check());
          assert(tableau.pivot_count() == before + 1);
          assert(tableau.is_basic(x) && !tableau.is_basic(s));
          assert(tableau.assignment(x) == target.scaled(Rational(direction)));
          assert(tableau.assignment(y) == rat(0));
          assert(tableau.assignment(s) == target);
          assert(tableau.assignment(r) == tableau.assignment(x).scaled(Rational(row_sign)));
          assert(tableau.assignment(t) == tableau.assignment(x).scaled(Rational(-row_sign)));

          tableau.pop(checkpoint);
          assert(!tableau.is_basic(x) && tableau.is_basic(s));
          assert(tableau.assignment(x) == rat(0));
        }
      }
    }
  }
}

void soi_zero_breakpoints_moving_outward_change_the_slope() {
  // Repairing s by moving x would violate the tight row r twice as fast.
  // The minimum is at step zero, including for a tight lower bound.
  for (const int direction : {-1, 1}) {
    for (const int row_sign : {-1, 1}) {
      LraTableau tableau(LraTableau::PivotingRule::Soi);
      const LraVarId x = tableau.add_variable();
      const LraVarId y = tableau.add_variable();
      const LraVarId s = tableau.add_variable();
      const LraVarId r = tableau.add_variable();
      tableau.add_row(s, {{x, Rational(direction)}, {y, Rational(1)}});
      tableau.add_row(r, {{x, Rational(2 * row_sign)}, {y, Rational(-1)}});
      assert(!tableau.assert_lower(y, rat(0), Lit{1}));
      assert(!tableau.assert_upper(y, rat(0), Lit{2}));
      assert(!tableau.assert_lower(s, rat(1), Lit{3}));
      if (direction == row_sign) {
        assert(!tableau.assert_upper(r, rat(0), Lit{4}));
      } else {
        assert(!tableau.assert_lower(r, rat(0), Lit{4}));
      }
      const auto conflict = tableau.check();
      assert(conflict && has_reason(*conflict, Lit{3}) && has_reason(*conflict, Lit{4}));
      assert(tableau.pivot_count() == 1);
      assert(tableau.assignment(x) == rat(0));
      assert(tableau.is_basic(x) && !tableau.is_basic(r));
    }
  }
}

void soi_fixed_variable_at_zero_contributes_one_direction_change() {
  // V(h) = max(3 - 3h, 0) + |2h| for h >= 0. Its minimum is at h=1.
  // Counting both initially coincident bounds would incorrectly prefer h=0.
  for (const int direction : {-1, 1}) {
    LraTableau tableau(LraTableau::PivotingRule::Soi);
    const LraVarId x = tableau.add_variable();
    const LraVarId y = tableau.add_variable();
    const LraVarId s = tableau.add_variable();
    const LraVarId t = tableau.add_variable();
    tableau.add_row(s, {{x, Rational(3 * direction)}, {y, Rational(1)}});
    tableau.add_row(t, {{x, Rational(2 * direction)}, {y, Rational(-1)}});
    assert(!tableau.assert_lower(y, rat(0), Lit{1}));
    assert(!tableau.assert_upper(y, rat(0), Lit{2}));
    assert(!tableau.assert_lower(s, rat(3), Lit{3}));
    assert(!tableau.assert_lower(t, rat(0), Lit{4}));
    assert(!tableau.assert_upper(t, rat(0), Lit{5}));
    assert(tableau.check());
    assert(tableau.pivot_count() == 1);
    assert(tableau.assignment(x) == rat(direction));
    assert(tableau.is_basic(x) && !tableau.is_basic(s));
  }
}

void soi_fixed_variable_at_nonzero_contributes_both_direction_changes() {
  // V(h) = max(3 - h, 0) + |h - 1|. Reaching the fixed value at h=1
  // changes its direction from -1 to +1. V is then flat until h=3, so
  // the nearest-breakpoint tie-break must choose h=1 in either direction.
  for (const int direction : {-1, 1}) {
    LraTableau tableau(LraTableau::PivotingRule::Soi);
    const LraVarId x = tableau.add_variable();
    const LraVarId y = tableau.add_variable();
    const LraVarId s = tableau.add_variable();
    const LraVarId t = tableau.add_variable();
    tableau.add_row(s, {{x, Rational(direction)}, {y, Rational(1)}});
    tableau.add_row(t, {{x, Rational(direction)}, {y, Rational(-1)}});
    assert(!tableau.assert_lower(y, rat(0), Lit{1}));
    assert(!tableau.assert_upper(y, rat(0), Lit{2}));
    assert(!tableau.assert_lower(s, rat(3), Lit{3}));
    assert(!tableau.assert_lower(t, rat(1), Lit{4}));
    assert(!tableau.assert_upper(t, rat(1), Lit{5}));
    assert(tableau.check());
    assert(tableau.pivot_count() == 1);
    assert(tableau.assignment(x) == rat(direction));
    assert(tableau.is_basic(x) && !tableau.is_basic(t));
  }
}

void soi_matches_bland_on_multi_pivot_scenarios() {
  // Mirrors satisfiable_system_via_a_slack_row/
  // infeasible_system_requires_a_pivot_to_discover under Soi.
  {
    LraTableau tableau(LraTableau::PivotingRule::Soi);
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
  }
  {
    LraTableau tableau(LraTableau::PivotingRule::Soi);
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
  }
}

void soi_push_pop_restores_state_across_a_pivot() {
  // Mirrors push_pop_restores_state_across_a_pivot exactly, under Soi --
  // directly targets the highest-risk, paper-uncovered piece (f_var_'s
  // row / soi_directions_ trail integration): after pop(), the tableau
  // must behave identically to having never pivoted under Soi at all.
  LraTableau tableau(LraTableau::PivotingRule::Soi);
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

  tableau.pop(checkpoint);
  assert(!tableau.check().has_value());  // feasible again

  // Redo the exact same assertion; must reproduce an equally sound
  // conflict (Soi's own conflict set may legitimately differ in size
  // from Bland's, but must still be sound and reproducible run to run).
  const auto redone_conflict = tableau.assert_lower(s2, rat(3), lower_reason);
  const auto redone_check_conflict =
      redone_conflict.has_value() ? redone_conflict : tableau.check();
  assert(redone_check_conflict.has_value());
  assert(has_reason(*redone_check_conflict, upper_reason));
  assert(has_reason(*redone_check_conflict, lower_reason));
}

void soi_multi_row_conflict_includes_every_violated_row() {
  // s1 = x+y, s2 = y+z, x>=8, z>=8, y>=5 -- both s1<=10 and s2<=10 are
  // simultaneously violated (13>10 each) with y shared between them.
  LraTableau tableau(LraTableau::PivotingRule::Soi);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId z = tableau.add_variable();
  const LraVarId s1 = tableau.add_variable();
  const LraVarId s2 = tableau.add_variable();
  tableau.add_row(s1, {{x, Rational(1)}, {y, Rational(1)}});
  tableau.add_row(s2, {{y, Rational(1)}, {z, Rational(1)}});

  const Lit x_lower{1};
  const Lit z_lower{2};
  const Lit y_lower{3};
  const Lit s1_upper{4};
  const Lit s2_upper{5};
  assert(!tableau.assert_lower(x, rat(8), x_lower).has_value());
  assert(!tableau.assert_lower(z, rat(8), z_lower).has_value());
  assert(!tableau.assert_lower(y, rat(5), y_lower).has_value());
  assert(!tableau.assert_upper(s1, rat(10), s1_upper).has_value());
  assert(!tableau.assert_upper(s2, rat(10), s2_upper).has_value());

  const auto conflict = tableau.check();
  assert(conflict.has_value());
  assert(tableau.soi_multi_row_conflict_count() == 1);
  // Sound regardless of exactly which rows minimize_soi_conflict kept:
  // every cited reason must be one of the five asserted above.
  for (const Lit reason : *conflict) {
    assert(reason.value == x_lower.value || reason.value == z_lower.value ||
           reason.value == y_lower.value || reason.value == s1_upper.value ||
           reason.value == s2_upper.value);
  }
}

void soi_quickxplain_minimizes_independent_conflicts() {
  // Three independently-pinned, independently-violated rows sharing no
  // variables -- each is already a complete conflict on its own (its
  // only term is pinned to a single point, no possible movement), so
  // minimize_soi_conflict must return exactly one of them, never all
  // three -- directly tests minimization behavior, not just soundness.
  LraTableau tableau(LraTableau::PivotingRule::Soi);
  const LraVarId a = tableau.add_variable();
  const LraVarId b = tableau.add_variable();
  const LraVarId c = tableau.add_variable();
  const LraVarId r1 = tableau.add_variable();
  const LraVarId r2 = tableau.add_variable();
  const LraVarId r3 = tableau.add_variable();
  tableau.add_row(r1, {{a, Rational(1)}});
  tableau.add_row(r2, {{b, Rational(1)}});
  tableau.add_row(r3, {{c, Rational(1)}});

  assert(!tableau.assert_lower(a, rat(0), Lit{1}).has_value());
  assert(!tableau.assert_upper(a, rat(0), Lit{2}).has_value());
  assert(!tableau.assert_lower(b, rat(0), Lit{3}).has_value());
  assert(!tableau.assert_upper(b, rat(0), Lit{4}).has_value());
  assert(!tableau.assert_lower(c, rat(0), Lit{5}).has_value());
  assert(!tableau.assert_upper(c, rat(0), Lit{6}).has_value());

  const Lit r1_reason{7};
  const Lit r2_reason{8};
  const Lit r3_reason{9};
  assert(!tableau.assert_lower(r1, rat(5), r1_reason).has_value());
  assert(!tableau.assert_lower(r2, rat(5), r2_reason).has_value());
  assert(!tableau.assert_lower(r3, rat(5), r3_reason).has_value());

  const auto conflict = tableau.check();
  assert(conflict.has_value());
  assert(tableau.soi_multi_row_conflict_count() == 1);
  const bool has_r1 = has_reason(*conflict, r1_reason);
  const bool has_r2 = has_reason(*conflict, r2_reason);
  const bool has_r3 = has_reason(*conflict, r3_reason);
  assert((has_r1 ? 1 : 0) + (has_r2 ? 1 : 0) + (has_r3 ? 1 : 0) == 1);
}

// -- Fixed-variable elimination (item H7) --

// s = x + y with s bounded on both sides at the same value. This is the
// shape every lowered Real equality produces (item H1b puts both halves on
// one shared slack), which is what makes it the case H7 exists for.
struct FixedSlackTableau {
  LraTableau tableau;
  LraVarId x = 0;
  LraVarId y = 0;
  LraVarId s = 0;

  explicit FixedSlackTableau(bool elimination) {
    tableau.set_fixed_elimination(elimination);
    x = tableau.add_variable();
    y = tableau.add_variable();
    s = tableau.add_variable();
    tableau.add_row(s, {{x, Rational(1)}, {y, Rational(1)}});
  }

  void fix_s_at(long value) {
    assert(!tableau.assert_upper(s, rat(value), Lit{1}).has_value());
    assert(!tableau.assert_lower(s, rat(value), Lit{2}).has_value());
  }
};

void fixed_basic_variable_is_pivoted_out_of_the_basis() {
  FixedSlackTableau fixture(/*elimination=*/true);
  fixture.fix_s_at(4);

  assert(fixture.tableau.fixed_variable_count() == 1);
  assert(fixture.tableau.elimination_pivot_count() == 1);
  // s left the basis and is pinned at its fixed value, which is what makes
  // it inert: can_enter() rejects a variable sitting on both its bounds.
  assert(!fixture.tableau.is_basic(fixture.s));
  assert(fixture.tableau.assignment(fixture.s) == rat(4));
  // Exactly one of x/y took its place; the other stayed non-basic.
  assert(fixture.tableau.is_basic(fixture.x) != fixture.tableau.is_basic(fixture.y));

  assert(!fixture.tableau.check().has_value());
  assert(fixture.tableau.assignment(fixture.x) + fixture.tableau.assignment(fixture.y) == rat(4));
}

void elimination_is_off_unless_enabled() {
  FixedSlackTableau fixture(/*elimination=*/false);
  fixture.fix_s_at(4);

  assert(fixture.tableau.fixed_variable_count() == 0);
  assert(fixture.tableau.elimination_pivot_count() == 0);
  assert(fixture.tableau.is_basic(fixture.s));
  assert(!fixture.tableau.check().has_value());
}

void elimination_prefers_an_unbounded_entering_candidate() {
  // s = 3x + y with y bounded and x not. Every other ranking criterion
  // favours y -- it occurs in one row against x's two, and its coefficient is
  // the unit one -- so this pins the unbounded-first preference specifically.
  // It matters because an unbounded basic variable's row can never be
  // violated, so it never demands another repair pivot.
  LraTableau tableau;
  tableau.set_fixed_elimination(true);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId z = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  const LraVarId t = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(3)}, {y, Rational(1)}});
  tableau.add_row(t, {{x, Rational(1)}, {z, Rational(1)}});

  assert(!tableau.assert_lower(y, rat(-100), Lit{1}).has_value());
  assert(!tableau.assert_upper(s, rat(6), Lit{2}).has_value());
  assert(!tableau.assert_lower(s, rat(6), Lit{3}).has_value());

  assert(tableau.elimination_pivot_count() == 1);
  assert(tableau.is_basic(x));
  assert(!tableau.is_basic(y));
  assert(!tableau.check().has_value());
}

void elimination_never_pivots_an_already_fixed_variable_into_the_basis() {
  // Same shape, but now y is *fixed* rather than merely bounded. Without the
  // explicit guard it is exactly what the ranking picks (fewest occurrences,
  // unit coefficient), and the elimination would hand back a fixed *basic*
  // variable: the permanently-repair-demanding state it was called to
  // remove, just moved to another row.
  LraTableau tableau;
  tableau.set_fixed_elimination(true);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId z = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  const LraVarId t = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(3)}, {y, Rational(1)}});
  tableau.add_row(t, {{x, Rational(1)}, {z, Rational(1)}});

  // x is bounded too, so the unbounded-first criterion cannot decide this
  // one and the fixed-candidate guard is what the outcome rests on.
  assert(!tableau.assert_lower(x, rat(-100), Lit{1}).has_value());
  assert(!tableau.assert_upper(y, rat(0), Lit{2}).has_value());
  assert(!tableau.assert_lower(y, rat(0), Lit{3}).has_value());

  assert(!tableau.assert_upper(s, rat(6), Lit{4}).has_value());
  assert(!tableau.assert_lower(s, rat(6), Lit{5}).has_value());
  assert(tableau.elimination_pivot_count() == 1);
  assert(tableau.is_basic(x));
  assert(!tableau.is_basic(y));
  assert(!tableau.is_basic(s));
  assert(!tableau.check().has_value());
  assert(tableau.assignment(x).scaled(Rational(3)) + tableau.assignment(y) == rat(6));
}

void conflict_is_still_found_through_an_eliminated_variable() {
  // s1 = x + y fixed at 0 forces y = -x and pivots x into the basis. s2 =
  // x - y is then rewritten in terms of what is left, and s3 = y keeps a
  // second constraint on it. s2 >= 10 and s3 >= 0 are jointly unsatisfiable
  // (s2 >= 10 means y <= -5), and the conflict has to survive the fact that
  // the row that produced it no longer looks the way it was written.
  LraTableau tableau;
  tableau.set_fixed_elimination(true);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s1 = tableau.add_variable();
  const LraVarId s2 = tableau.add_variable();
  const LraVarId s3 = tableau.add_variable();
  tableau.add_row(s1, {{x, Rational(1)}, {y, Rational(1)}});
  tableau.add_row(s2, {{x, Rational(1)}, {y, Rational(-1)}});
  tableau.add_row(s3, {{y, Rational(1)}});

  const Lit s1_upper{1};
  const Lit s1_lower{2};
  const Lit s2_lower{3};
  const Lit s3_lower{4};
  assert(!tableau.assert_upper(s1, rat(0), s1_upper).has_value());
  assert(!tableau.assert_lower(s1, rat(0), s1_lower).has_value());
  assert(tableau.elimination_pivot_count() == 1);

  assert(!tableau.assert_lower(s2, rat(10), s2_lower).has_value());
  assert(!tableau.assert_lower(s3, rat(0), s3_lower).has_value());

  const auto conflict = tableau.check();
  assert(conflict.has_value());
  assert(has_reason(*conflict, s2_lower));
  assert(has_reason(*conflict, s3_lower));
  // Every literal in the certificate must be one actually asserted -- a
  // Farkas certificate naming an unasserted bound would be unsound.
  for (const Lit reason : *conflict) {
    assert(reason.value == s1_upper.value || reason.value == s1_lower.value ||
           reason.value == s2_lower.value || reason.value == s3_lower.value);
  }
  (void)x;
}

void push_pop_restores_state_across_an_elimination_pivot() {
  FixedSlackTableau fixture(/*elimination=*/true);
  LraTableau& tableau = fixture.tableau;

  const std::size_t checkpoint = tableau.push();
  fixture.fix_s_at(4);
  assert(tableau.elimination_pivot_count() == 1);

  tableau.pop(checkpoint);
  // The elimination pivot is deliberately *not* undone -- bounds-only
  // backtracking leaves the basis wherever the search left it. What has to
  // hold is the pair of invariants that makes that sound, plus the bounds
  // actually being gone.
  assert_tableau_invariants(tableau);

  // s was fixed at 4 inside the level; after the pop that is gone, so a
  // conflicting bound is now assertable where it would have clashed before.
  assert(!tableau.assert_upper(fixture.s, rat(1), Lit{6}).has_value());
  assert(!tableau.assert_lower(fixture.x, rat(3), Lit{5}).has_value());

  // And the tableau still solves: the row equation holds, s respects its
  // new bound, and nothing is left violated.
  assert(!tableau.check().has_value());
  assert_tableau_invariants(tableau);
  assert(tableau.assignment(fixture.s) <= rat(1));
  assert(tableau.assignment(fixture.x) + tableau.assignment(fixture.y) ==
         tableau.assignment(fixture.s));
}

void elimination_agrees_with_the_ablation_on_multi_pivot_scenarios() {
  // Same scenarios sparse_matches_bland_on_multi_pivot_scenarios covers,
  // re-run with an equality fixing the shared slack so elimination fires,
  // under both pivoting rules the solver ships.
  for (const LraTableau::PivotingRule rule :
       {LraTableau::PivotingRule::Bland, LraTableau::PivotingRule::Sparse}) {
    for (const long fixed_value : {-6L, 0L, 3L, 12L}) {
      bool verdicts[2] = {false, false};
      for (int variant = 0; variant < 2; ++variant) {
        LraTableau tableau(rule);
        tableau.set_fixed_elimination(variant == 0);
        const LraVarId x = tableau.add_variable();
        const LraVarId y = tableau.add_variable();
        const LraVarId z = tableau.add_variable();
        const LraVarId s1 = tableau.add_variable();
        const LraVarId s2 = tableau.add_variable();
        const LraVarId s3 = tableau.add_variable();
        tableau.add_row(s1, {{x, Rational(1)}, {y, Rational(2)}, {z, Rational(-1)}});
        tableau.add_row(s2, {{x, Rational(3)}, {z, Rational(1)}});
        tableau.add_row(s3, {{y, Rational(1)}, {z, Rational(4)}});

        assert(!tableau.assert_upper(s1, rat(fixed_value), Lit{1}).has_value());
        assert(!tableau.assert_lower(s1, rat(fixed_value), Lit{2}).has_value());
        assert(!tableau.assert_lower(s2, rat(4), Lit{3}).has_value());
        assert(!tableau.assert_upper(s2, rat(9), Lit{4}).has_value());
        assert(!tableau.assert_lower(s3, rat(2), Lit{5}).has_value());
        const auto conflict = tableau.check();
        verdicts[variant] = conflict.has_value();
        if (!conflict.has_value()) {
          // Every row equation must still hold for the final assignment.
          for (LraVarId var : {s1, s2, s3}) {
            if (!tableau.is_basic(var)) {
              continue;
            }
            DeltaRational sum = rat(0);
            for (const auto& [term_var, coeff, term_slot] : tableau.row(var)) {
              sum = sum + tableau.assignment(term_var).scaled(coeff);
            }
            assert(sum == tableau.assignment(var));
          }
        }
      }
      assert(verdicts[0] == verdicts[1]);
    }
  }
}

void rows_stay_ordered_and_nonzero_through_pivoting() {
  // pivot()'s affected-row loop combines two rows with a linear merge, which
  // is correct only if both inputs are ordered by var id and hold no zero
  // coefficient -- the Row invariant (see its declaration). A violation
  // would not throw; it would silently produce a wrong row and therefore a
  // wrong verdict, so this checks the invariant directly on every basic row
  // after every step rather than inferring it from a verdict.
  //
  // add_row() is deliberately given terms in *descending* var id here: the
  // real frontend passes them in canonical-polynomial order, which has no
  // relation to var-id order, so a producer that merely preserved caller
  // order would pass a test that fed it sorted input.
  const auto check_invariant = [](const LraTableau& tableau, const char* where) {
    for (LraVarId var = 0; var < 3 + 3; ++var) {
      if (!tableau.is_basic(var)) {
        continue;
      }
      const auto& row = tableau.row(var);
      for (std::size_t i = 0; i < row.size(); ++i) {
        assert(!row[i].coeff.is_zero() && where != nullptr);
        if (i > 0) {
          assert(row[i - 1].var < row[i].var);
        }
      }
    }
  };

  for (const LraTableau::PivotingRule rule :
       {LraTableau::PivotingRule::Bland, LraTableau::PivotingRule::Sparse,
        LraTableau::PivotingRule::Dantzig, LraTableau::PivotingRule::Soi}) {
    LraTableau tableau(rule);
    const LraVarId x = tableau.add_variable();
    const LraVarId y = tableau.add_variable();
    const LraVarId z = tableau.add_variable();
    const LraVarId s1 = tableau.add_variable();
    const LraVarId s2 = tableau.add_variable();
    const LraVarId s3 = tableau.add_variable();

    tableau.add_row(s1, {{z, Rational(-1)}, {y, Rational(2)}, {x, Rational(1)}});
    tableau.add_row(s2, {{z, Rational(1)}, {x, Rational(3)}});
    tableau.add_row(s3, {{z, Rational(4)}, {y, Rational(1)}});
    check_invariant(tableau, "after add_row");

    assert(!tableau.assert_lower(s1, rat(5), Lit{1}).has_value());
    assert(!tableau.assert_upper(s2, rat(2), Lit{2}).has_value());
    assert(!tableau.assert_lower(s3, rat(1), Lit{3}).has_value());
    tableau.check();
    check_invariant(tableau, "after check");

    // Push/pop restores rows wholesale from the trail, so the invariant has
    // to survive a backtrack too.
    const std::size_t checkpoint = tableau.push();
    assert(!tableau.assert_upper(s1, rat(30), Lit{4}).has_value());
    assert(!tableau.assert_lower(s2, rat(-30), Lit{5}).has_value());
    tableau.check();
    check_invariant(tableau, "after nested check");
    tableau.pop(checkpoint);
    check_invariant(tableau, "after pop");
  }
}

void merge_and_sort_rebuilds_agree_on_a_cancelling_pivot() {
  // The merge's one branch that can drop a term is the equal-var-id case,
  // where the carried coefficient and the scaled entering coefficient cancel
  // exactly. This builds rows whose pivot produces such a cancellation and
  // pins that the resulting row omits the cancelled variable entirely
  // (rather than storing a zero, which would break the invariant and leave a
  // stale occurs_in_ entry behind).
  LraTableau tableau(LraTableau::PivotingRule::Bland);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s1 = tableau.add_variable();
  const LraVarId s2 = tableau.add_variable();

  // s1 = x + y, s2 = x - y. Pivoting x into s1's row rewrites s2 as
  // s2 = s1 - 2y, and pivoting y in turn cancels terms between them.
  tableau.add_row(s1, {{x, Rational(1)}, {y, Rational(1)}});
  tableau.add_row(s2, {{x, Rational(1)}, {y, Rational(-1)}});

  assert(!tableau.assert_lower(s1, rat(4), Lit{1}).has_value());
  assert(!tableau.assert_upper(s1, rat(4), Lit{2}).has_value());
  assert(!tableau.assert_lower(s2, rat(0), Lit{3}).has_value());
  assert(!tableau.check().has_value());

  for (const LraVarId var : {x, y, s1, s2}) {
    if (!tableau.is_basic(var)) {
      continue;
    }
    DeltaRational sum = rat(0);
    for (const auto& [term_var, coeff, term_slot] : tableau.row(var)) {
      assert(!coeff.is_zero());
      sum = sum + tableau.assignment(term_var).scaled(coeff);
    }
    assert(sum == tableau.assignment(var));
  }
}

void delta_maintained_occurs_in_matches_a_full_rebuild() {
  // pivot() maintains occurs_in_ by delta: a term carried through a pivot
  // keeps its entry untouched, and only variables that genuinely enter a
  // row (from the entering row) or leave it (cancelled to zero) are
  // updated. That replaced removing every term's entry and re-adding it,
  // which callgrind put at ~91% of the program's instructions.
  //
  // A delta that is wrong in either direction -- a stale entry left behind,
  // or a live one dropped -- does not throw. It makes update() skip a
  // dependent row or sparse pricing misjudge a column, and surfaces much
  // later as a wrong answer. So this checks the index against a full
  // rebuild after *every* pivot, across all four pivoting rules and a range
  // of bound combinations that produce different pivot sequences.
  for (const LraTableau::PivotingRule rule :
       {LraTableau::PivotingRule::Bland, LraTableau::PivotingRule::Sparse,
        LraTableau::PivotingRule::Dantzig, LraTableau::PivotingRule::Soi}) {
    for (const long bound : {-9L, -1L, 0L, 3L, 8L, 21L}) {
      LraTableau tableau(rule);
      const LraVarId x = tableau.add_variable();
      const LraVarId y = tableau.add_variable();
      const LraVarId z = tableau.add_variable();
      const LraVarId w = tableau.add_variable();
      const LraVarId s1 = tableau.add_variable();
      const LraVarId s2 = tableau.add_variable();
      const LraVarId s3 = tableau.add_variable();

      // Deliberately overlapping supports, so pivots produce shared terms
      // (the "carried through, touch nothing" case) as well as terms that
      // are new to a row, which is the pair the delta has to tell apart.
      tableau.add_row(s1,
                      {{w, Rational(1)}, {z, Rational(-1)}, {y, Rational(2)}, {x, Rational(1)}});
      tableau.add_row(s2, {{z, Rational(1)}, {x, Rational(3)}, {w, Rational(-2)}});
      tableau.add_row(s3, {{y, Rational(1)}, {z, Rational(4)}, {x, Rational(-3)}});
      assert(tableau.occurs_in_index_matches_full_rebuild());

      assert(!tableau.assert_lower(s1, rat(bound), Lit{1}).has_value());
      assert(!tableau.assert_upper(s2, rat(bound + 4), Lit{2}).has_value());
      tableau.check();
      assert(tableau.occurs_in_index_matches_full_rebuild());

      const std::size_t checkpoint = tableau.push();
      assert(!tableau.assert_lower(s3, rat(bound + 2), Lit{3}).has_value());
      assert(!tableau.assert_upper(s1, rat(bound + 25), Lit{4}).has_value());
      tableau.check();
      assert(tableau.occurs_in_index_matches_full_rebuild());

      tableau.pop(checkpoint);
      assert(tableau.occurs_in_index_matches_full_rebuild());
      tableau.check();
      assert(tableau.occurs_in_index_matches_full_rebuild());
      assert_tableau_invariants(tableau);
    }
  }
}

void cancelling_pivot_drops_the_occurrence_entry() {
  // The one case where the delta must *remove*: a shared term whose
  // coefficient cancels to exactly zero leaves the row, so its occurrence
  // entry has to go with it. Getting this wrong leaves a stale entry that
  // makes update() walk a row no longer mentioning the variable.
  LraTableau tableau(LraTableau::PivotingRule::Bland);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s1 = tableau.add_variable();
  const LraVarId s2 = tableau.add_variable();
  tableau.add_row(s1, {{x, Rational(1)}, {y, Rational(1)}});
  tableau.add_row(s2, {{x, Rational(1)}, {y, Rational(-1)}});

  assert(!tableau.assert_lower(s1, rat(4), Lit{1}).has_value());
  assert(!tableau.assert_upper(s1, rat(4), Lit{2}).has_value());
  assert(!tableau.assert_lower(s2, rat(0), Lit{3}).has_value());
  assert(!tableau.check().has_value());

  assert(tableau.occurs_in_index_matches_full_rebuild());
  assert_tableau_invariants(tableau);
  // No stored coefficient may be zero -- a cancelled term must be absent
  // from the row, not present with a zero value.
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) {
    if (!tableau.is_basic(var)) {
      continue;
    }
    for (const auto& [term_var, coeff, term_slot] : tableau.row(var)) {
      assert(!coeff.is_zero());
      (void)term_var;
    }
  }
}

// -- Nelson-Oppen equality sharing: model_equal_classes --
//
// These pin the M-Propagate trigger from docs/qf-lra-equality-sharing.md. The
// contract is deliberately narrow: partition ONLY the shared variables, by the
// current model, returning candidates for the combination layer to branch on.

void model_equal_classes_groups_shared_variables_by_model_value() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId z = tableau.add_variable();
  // x and y pinned to 5, z to 7.
  assert(!tableau.assert_lower(x, rat(5), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(5), Lit{2}).has_value());
  assert(!tableau.assert_lower(y, rat(5), Lit{3}).has_value());
  assert(!tableau.assert_upper(y, rat(5), Lit{4}).has_value());
  assert(!tableau.assert_lower(z, rat(7), Lit{5}).has_value());
  assert(!tableau.check().has_value());

  const auto classes = tableau.model_equal_classes({x, y, z});
  // One class {x, y}; z is a singleton and must not be reported.
  assert(classes.size() == 1);
  assert(classes[0].size() == 2);
  assert(classes[0][0] == x && classes[0][1] == y);
}

// Scoping is the point: a variable that coincides in value but is NOT shared
// must not appear. The combination layer only cares about interface variables,
// and returning others would be noise it has to filter.
void model_equal_classes_ignores_variables_outside_the_shared_set() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId hidden = tableau.add_variable();
  for (const LraVarId v : {x, y, hidden}) {
    assert(!tableau.assert_lower(v, rat(3), Lit{static_cast<int>(v) + 1}).has_value());
    assert(!tableau.assert_upper(v, rat(3), Lit{static_cast<int>(v) + 10}).has_value());
  }
  assert(!tableau.check().has_value());

  const auto classes = tableau.model_equal_classes({x, y});
  assert(classes.size() == 1);
  assert(classes[0].size() == 2);  // hidden shares the value but is not shared
}

// The delta trap. A strict bound gives a variable an infinitesimal component,
// and two variables agreeing on the rational part while differing in delta are
// NOT equal in the concrete model the delta encoding stands for. Keying the
// partition on the rational part alone would report an equality that does not
// hold -- an unsound propagation handed to the combination layer.
void model_equal_classes_separates_values_differing_only_in_delta() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  // x = 4 exactly; y > 4, which the delta encoding represents as the lower
  // bound 4 + delta (the same idiom as
  // strict_inequality_conflicts_with_touching_nonstrict_bound above).
  const DeltaRational strict_lower{Rational(4), Rational(1)};
  assert(!tableau.assert_lower(x, rat(4), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(4), Lit{2}).has_value());
  assert(!tableau.assert_lower(y, strict_lower, Lit{3}).has_value());
  assert(!tableau.check().has_value());

  assert(tableau.assignment(x).rational == tableau.assignment(y).rational);
  assert(tableau.assignment(x) != tableau.assignment(y));
  assert(tableau.model_equal_classes({x, y}).empty());
}

void model_equal_classes_is_empty_when_nothing_coincides() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  assert(!tableau.assert_lower(x, rat(1), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(1), Lit{2}).has_value());
  assert(!tableau.assert_lower(y, rat(2), Lit{3}).has_value());
  assert(!tableau.assert_upper(y, rat(2), Lit{4}).has_value());
  assert(!tableau.check().has_value());
  assert(tableau.model_equal_classes({x, y}).empty());
  assert(tableau.model_equal_classes({}).empty());
}

// Duplicates are the caller's business; each variable appears once.
void model_equal_classes_deduplicates_the_shared_set() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  for (const LraVarId v : {x, y}) {
    assert(!tableau.assert_lower(v, rat(9), Lit{static_cast<int>(v) + 1}).has_value());
    assert(!tableau.assert_upper(v, rat(9), Lit{static_cast<int>(v) + 10}).has_value());
  }
  assert(!tableau.check().has_value());
  const auto classes = tableau.model_equal_classes({x, y, x, y, x});
  assert(classes.size() == 1);
  assert(classes[0].size() == 2);
}

// The classes track the MODEL, so they must refine as bounds tighten. This is
// what makes them a live trigger rather than a one-shot computation.
void model_equal_classes_track_the_model_across_assertions() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  assert(!tableau.check().has_value());
  // Both start at 0, so they coincide.
  assert(tableau.model_equal_classes({x, y}).size() == 1);
  // Push y away; the candidate must disappear.
  assert(!tableau.assert_lower(y, rat(6), Lit{1}).has_value());
  assert(!tableau.check().has_value());
  assert(tableau.model_equal_classes({x, y}).empty());
}

// -- entailed_equalities: FixedEq and EqRow --
//
// Unlike model_equal_classes these are ENTAILMENTS carrying reasons, so the
// tests assert the reason literals, not just that a pair came back.

bool has_equality(const std::vector<LraTableau::EntailedEquality>& eqs, LraVarId a, LraVarId b) {
  for (const auto& e : eqs) {
    if ((e.lhs == a && e.rhs == b) || (e.lhs == b && e.rhs == a)) {
      return true;
    }
  }
  return false;
}

const std::vector<Lit>* reasons_for(const std::vector<LraTableau::EntailedEquality>& eqs,
                                    LraVarId a, LraVarId b) {
  for (const auto& e : eqs) {
    if ((e.lhs == a && e.rhs == b) || (e.lhs == b && e.rhs == a)) {
      return &e.reasons;
    }
  }
  return nullptr;
}

void fixedeq_reports_variables_pinned_to_the_same_value() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const Lit xl{1};
  const Lit xu{2};
  const Lit yl{3};
  const Lit yu{4};
  assert(!tableau.assert_lower(x, rat(5), xl).has_value());
  assert(!tableau.assert_upper(x, rat(5), xu).has_value());
  assert(!tableau.assert_lower(y, rat(5), yl).has_value());
  assert(!tableau.assert_upper(y, rat(5), yu).has_value());

  const auto eqs = tableau.entailed_equalities({x, y});
  assert(eqs.size() == 1);
  assert(has_equality(eqs, x, y));
  // All four bounds are what force it, and all four must appear.
  const std::vector<Lit>* reasons = reasons_for(eqs, x, y);
  assert(reasons != nullptr);
  assert(reasons->size() == 4);
  assert(has_reason(*reasons, xl) && has_reason(*reasons, xu));
  assert(has_reason(*reasons, yl) && has_reason(*reasons, yu));
}

// Pinned to DIFFERENT values: no equality, however close.
void fixedeq_does_not_report_different_fixed_values() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  assert(!tableau.assert_lower(x, rat(5), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(5), Lit{2}).has_value());
  assert(!tableau.assert_lower(y, rat(6), Lit{3}).has_value());
  assert(!tableau.assert_upper(y, rat(6), Lit{4}).has_value());
  assert(tableau.entailed_equalities({x, y}).empty());
}

// THE HAZARD the plan named: a variable with only one bound is ordinary, not a
// precondition violation. build_conflict may dereference ->reason blind because
// its caller established that no entering candidate exists; this runs
// speculatively and must simply decline.
void entailed_equalities_tolerates_variables_with_one_or_no_bound() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId z = tableau.add_variable();
  // x fully pinned; y only lower-bounded at the same value; z unbounded.
  assert(!tableau.assert_lower(x, rat(2), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(2), Lit{2}).has_value());
  assert(!tableau.assert_lower(y, rat(2), Lit{3}).has_value());
  // Must not crash, and must not claim x = y: y may still exceed 2.
  const auto eqs = tableau.entailed_equalities({x, y, z});
  assert(eqs.empty());
}

// EqRow: s = x with nothing else in the row. P is empty, so the equality holds
// unconditionally and the reason set is legitimately EMPTY.
void eqrow_reports_a_bare_defining_row_with_no_reasons() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}});

  const auto eqs = tableau.entailed_equalities({s, x});
  assert(eqs.size() == 1);
  assert(has_equality(eqs, s, x));
  const std::vector<Lit>* reasons = reasons_for(eqs, s, x);
  assert(reasons != nullptr);
  assert(reasons->empty());  // no asserted bound is involved
}

// EqRow with a genuine P: s = x + d, d pinned to 0, so s = x and the reason is
// exactly the pair of bounds pinning d.
void eqrow_reports_when_the_remainder_is_fixed_at_zero() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId d = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}, {d, Rational(1)}});
  const Lit dl{7};
  const Lit du{8};
  assert(!tableau.assert_lower(d, rat(0), dl).has_value());
  assert(!tableau.assert_upper(d, rat(0), du).has_value());

  const auto eqs = tableau.entailed_equalities({s, x});
  assert(has_equality(eqs, s, x));
  const std::vector<Lit>* reasons = reasons_for(eqs, s, x);
  assert(reasons != nullptr);
  assert(reasons->size() == 2);
  assert(has_reason(*reasons, dl) && has_reason(*reasons, du));
}

// The same row with d pinned to a NON-zero value entails s = x + 3, not s = x.
void eqrow_declines_when_the_remainder_is_fixed_nonzero() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId d = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}, {d, Rational(1)}});
  assert(!tableau.assert_lower(d, rat(3), Lit{1}).has_value());
  assert(!tableau.assert_upper(d, rat(3), Lit{2}).has_value());
  assert(tableau.entailed_equalities({s, x}).empty());
}

// d unpinned: nothing is entailed even though beta might currently agree.
void eqrow_declines_when_the_remainder_is_not_fixed() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId d = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}, {d, Rational(1)}});
  assert(!tableau.assert_lower(d, rat(0), Lit{1}).has_value());  // lower only
  assert(tableau.entailed_equalities({s, x}).empty());
}

// Coefficient -1 gives s = -x + P, which is not an equality between s and x.
void eqrow_declines_on_a_negated_coefficient() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(-1)}});
  assert(tableau.entailed_equalities({s, x}).empty());
}

// Scoping again: an entailed equality involving a non-shared variable is not
// the combination layer's business and must not be reported.
void entailed_equalities_respects_the_shared_set() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}});
  assert(tableau.entailed_equalities({s}).empty());  // x not shared
  assert(tableau.entailed_equalities({x}).empty());  // s not shared
  assert(tableau.entailed_equalities({s, x}).size() == 1);
}

// A spanning set, not all pairs: three mutually equal variables give two
// equalities, and transitivity on the other side recovers the third.
void entailed_equalities_emits_a_spanning_set_not_all_pairs() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId z = tableau.add_variable();
  int lit = 1;
  for (const LraVarId v : {x, y, z}) {
    assert(!tableau.assert_lower(v, rat(4), Lit{lit++}).has_value());
    assert(!tableau.assert_upper(v, rat(4), Lit{lit++}).has_value());
  }
  const auto eqs = tableau.entailed_equalities({x, y, z});
  assert(eqs.size() == 2);  // not 3
  assert(has_equality(eqs, x, y));
  assert(has_equality(eqs, x, z));
}

void eqoffsetrows_reports_matching_fixed_offsets() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId common = tableau.add_variable();
  const LraVarId p = tableau.add_variable();
  const LraVarId q = tableau.add_variable();
  tableau.add_row(x, {{common, Rational(1)}, {p, Rational(1)}});
  tableau.add_row(y, {{common, Rational(1)}, {q, Rational(1)}});
  assert(!tableau.assert_lower(p, rat(2), Lit{31}).has_value());
  assert(!tableau.assert_upper(p, rat(2), Lit{32}).has_value());
  assert(!tableau.assert_lower(q, rat(2), Lit{33}).has_value());
  assert(!tableau.assert_upper(q, rat(2), Lit{34}).has_value());
  assert(!tableau.check().has_value());

  const auto equalities = tableau.entailed_equalities({x, y});
  const auto found = std::find_if(equalities.begin(), equalities.end(), [](const auto& equality) {
    return equality.rule == LraTableau::EntailedEqualityRule::EqOffsetRows;
  });
  assert(found != equalities.end());
  assert(found->lhs == x && found->rhs == y);
  assert(found->reasons.size() == 4);
  for (std::size_t index = 0; index < found->reasons.size(); ++index) {
    assert(found->reasons[index].value == static_cast<int>(31 + index));
  }
}

void eqoffsetrows_rejects_different_offsets() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId common = tableau.add_variable();
  const LraVarId p = tableau.add_variable();
  const LraVarId q = tableau.add_variable();
  tableau.add_row(x, {{common, Rational(1)}, {p, Rational(1)}});
  tableau.add_row(y, {{common, Rational(1)}, {q, Rational(1)}});
  assert(!tableau.assert_lower(p, rat(2), Lit{31}).has_value());
  assert(!tableau.assert_upper(p, rat(2), Lit{32}).has_value());
  assert(!tableau.assert_lower(q, rat(3), Lit{33}).has_value());
  assert(!tableau.assert_upper(q, rat(3), Lit{34}).has_value());
  assert(!tableau.check().has_value());

  const auto equalities = tableau.entailed_equalities({x, y});
  assert(std::none_of(equalities.begin(), equalities.end(), [](const auto& equality) {
    return equality.rule == LraTableau::EntailedEqualityRule::EqOffsetRows;
  }));
}

void eqrows_reports_matching_polynomials_and_emits_a_spanning_set() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId z = tableau.add_variable();
  const LraVarId u = tableau.add_variable();
  const LraVarId v = tableau.add_variable();
  const LraVarId p = tableau.add_variable();
  const LraVarId q = tableau.add_variable();
  const LraVarId r = tableau.add_variable();
  tableau.add_row(x, {{u, Rational(2)}, {v, Rational(-3)}, {p, Rational(1)}});
  tableau.add_row(y, {{u, Rational(2)}, {v, Rational(-3)}, {q, Rational(1)}});
  tableau.add_row(z, {{u, Rational(2)}, {v, Rational(-3)}, {r, Rational(1)}});
  int reason = 41;
  for (const LraVarId fixed : {p, q, r}) {
    assert(!tableau.assert_lower(fixed, rat(5), Lit{reason++}).has_value());
    assert(!tableau.assert_upper(fixed, rat(5), Lit{reason++}).has_value());
  }
  assert(!tableau.check().has_value());

  const auto equalities = tableau.entailed_equalities({x, y, z});
  std::vector<LraTableau::EntailedEquality> eqrows;
  std::copy_if(equalities.begin(), equalities.end(), std::back_inserter(eqrows),
               [](const auto& equality) {
                 return equality.rule == LraTableau::EntailedEqualityRule::EqRows;
               });
  assert(eqrows.size() == 2);
  assert(eqrows[0].lhs == x && eqrows[0].rhs == y);
  assert(eqrows[1].lhs == x && eqrows[1].rhs == z);
}

void eqrows_rejects_different_nonfixed_polynomials() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId u = tableau.add_variable();
  const LraVarId v = tableau.add_variable();
  tableau.add_row(x, {{u, Rational(2)}, {v, Rational(-3)}});
  tableau.add_row(y, {{u, Rational(2)}, {v, Rational(-2)}});
  assert(!tableau.check().has_value());

  const auto equalities = tableau.entailed_equalities({x, y});
  assert(std::none_of(equalities.begin(), equalities.end(), [](const auto& equality) {
    return equality.rule == LraTableau::EntailedEqualityRule::EqRows;
  }));
}

void model_unit_filter_requires_repair_after_an_infeasible_branch() {
  LraTableau tableau;
  const LraVarId y = tableau.add_variable();
  const LraVarId z = tableau.add_variable();
  const LraVarId x = tableau.add_variable();
  const LraVarId u = tableau.add_variable();
  tableau.add_row(x, {{y, Rational(1)}, {z, Rational(1)}});
  assert(!tableau.assert_lower(z, rat(1), Lit{41}).has_value());
  assert(!tableau.assert_upper(z, rat(1), Lit{42}).has_value());
  assert(!tableau.assert_lower(x, rat(1), Lit{43}).has_value());
  assert(!tableau.assert_upper(x, rat(1), Lit{44}).has_value());
  assert(!tableau.assert_lower(u, rat(2), Lit{45}).has_value());
  assert(!tableau.assert_upper(u, rat(2), Lit{46}).has_value());
  assert(!tableau.check().has_value());
  assert(tableau.assignment(x) == rat(1));
  const auto checkpoint = tableau.push();
  assert(!tableau.assert_lower(y, rat(1), Lit{47}).has_value());
  assert(tableau.check().has_value());
  tableau.pop(checkpoint);
  // Bounds-only rollback retains the infeasible assignment x=u=2. Its
  // accidental model equality must not enter the paper's model-value filter:
  // the surviving bounds actually fix x=1 and u=2. The consumer must repair
  // first, even if no new arithmetic assignment arrives after this pop.
  assert(tableau.assignment(x) == rat(2));
  assert(tableau.assignment(u) == rat(2));
  assert(!tableau.check().has_value());
  assert(tableau.assignment(x) == rat(1));
  assert(tableau.assignment(y) == rat(0));
  assert(tableau.model_filtered_unit_equalities({x, u}).equalities.empty());
}

void model_unit_eqrow_avoids_large_fixed_offset_construction() {
  LraTableau tableau;
  const LraVarId y = tableau.add_variable();
  const LraVarId p = tableau.add_variable();
  const LraVarId q = tableau.add_variable();
  const LraVarId x = tableau.add_variable();
  tableau.add_row(x, {{y, Rational(1)}, {p, Rational(1)}, {q, Rational(-1)}});
  const Rational huge = *Rational::parse("100000000000000000000000000000000000000000000000001");
  const DeltaRational fixed_huge = DeltaRational::from_rational(huge);
  assert(!tableau.assert_lower(p, fixed_huge, Lit{51}).has_value());
  assert(!tableau.assert_upper(p, fixed_huge, Lit{52}).has_value());
  assert(!tableau.assert_lower(q, fixed_huge, Lit{53}).has_value());
  assert(!tableau.assert_upper(q, fixed_huge, Lit{54}).has_value());
  assert(!tableau.check().has_value());

  const auto query = tableau.model_filtered_unit_equalities({x, y});
  assert(query.stats.model_classes == 1);
  assert(query.stats.candidate_pairs == 1);
  assert(query.stats.rows_inspected == 1);
  assert(query.stats.nonzeros_inspected == 3);
  assert(query.stats.one_row_matches == 1);
  assert(query.equalities.size() == 1);
  assert(has_equality(query.equalities, x, y));
  assert(query.equalities.front().reasons.size() == 4);
}

void model_unit_two_row_accepts_both_same_unit_signs() {
  const auto run = [](long coefficient) {
    LraTableau tableau;
    const LraVarId connector = tableau.add_variable();
    const LraVarId p = tableau.add_variable();
    const LraVarId q = tableau.add_variable();
    const LraVarId x = tableau.add_variable();
    const LraVarId y = tableau.add_variable();
    tableau.add_row(x, {{connector, Rational(coefficient)}, {p, Rational(1)}});
    tableau.add_row(y, {{connector, Rational(coefficient)}, {q, Rational(1)}});
    assert(!tableau.assert_lower(p, rat(7), Lit{61}).has_value());
    assert(!tableau.assert_upper(p, rat(7), Lit{62}).has_value());
    assert(!tableau.assert_lower(q, rat(7), Lit{63}).has_value());
    assert(!tableau.assert_upper(q, rat(7), Lit{64}).has_value());
    assert(!tableau.check().has_value());

    const auto query = tableau.model_filtered_unit_equalities({x, y});
    assert(query.stats.two_row_matches == 1);
    assert(query.equalities.size() == 1);
    assert(has_equality(query.equalities, x, y));
    assert(query.equalities.front().reasons.size() == 4);
  };
  run(1);
  run(-1);
}

void model_unit_two_row_rejects_opposite_signs_and_nonunit_connectors() {
  {
    LraTableau tableau;
    const LraVarId z = tableau.add_variable();
    const LraVarId x = tableau.add_variable();
    const LraVarId y = tableau.add_variable();
    tableau.add_row(x, {{z, Rational(1)}});
    tableau.add_row(y, {{z, Rational(-1)}});
    const auto query = tableau.model_filtered_unit_equalities({x, y});
    assert(query.stats.model_classes == 1);
    assert(query.stats.two_row_matches == 0);
    assert(query.equalities.empty());
  }
  {
    LraTableau tableau;
    const LraVarId z = tableau.add_variable();
    const LraVarId x = tableau.add_variable();
    const LraVarId y = tableau.add_variable();
    tableau.add_row(x, {{z, Rational(2)}});
    tableau.add_row(y, {{z, Rational(2)}});
    const auto query = tableau.model_filtered_unit_equalities({x, y});
    assert(query.stats.nonunit_rejections == 2);
    assert(query.stats.two_row_matches == 0);
    assert(query.equalities.empty());
  }
}

void model_unit_output_is_a_deterministic_spanning_set() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId z = tableau.add_variable();
  int reason = 71;
  for (const LraVarId var : {x, y, z}) {
    assert(!tableau.assert_lower(var, rat(4), Lit{reason++}).has_value());
    assert(!tableau.assert_upper(var, rat(4), Lit{reason++}).has_value());
  }
  const auto first = tableau.model_filtered_unit_equalities({x, y, z});
  const auto second = tableau.model_filtered_unit_equalities({x, y, z});
  assert(first.equalities.size() == 2);
  assert(second.equalities.size() == first.equalities.size());
  for (std::size_t index = 0; index < first.equalities.size(); ++index) {
    assert(first.equalities[index].lhs == second.equalities[index].lhs);
    assert(first.equalities[index].rhs == second.equalities[index].rhs);
  }
}

void model_unit_entailment_disappears_when_a_fixing_bound_is_popped() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId d = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}, {d, Rational(1)}});
  const std::size_t checkpoint = tableau.push();
  assert(!tableau.assert_lower(d, rat(0), Lit{81}).has_value());
  assert(!tableau.assert_upper(d, rat(0), Lit{82}).has_value());
  assert(tableau.model_filtered_unit_equalities({s, x}).equalities.size() == 1);
  tableau.pop(checkpoint);
  assert(tableau.model_filtered_unit_equalities({s, x}).equalities.empty());
}

// -- Differential oracle for entailed_equalities --
//
// entailed_equalities applies deliberately incomplete rules. To test it we need
// an independent, COMPLETE notion of "x = y is entailed", and we have one: the
// tableau itself. Given a slack s = x - y, the equality x = y holds in every
// model iff neither s > 0 nor s < 0 is satisfiable. That is the same idea as
// lra_implied_real_equalities in check_sat.cpp -- which is complete but costs a
// whole fresh tableau per pair -- expressed against a tableau we already have.
//
// Soundness is the property under test: everything the cheap rules report MUST
// be entailed. The converse is not expected to hold, and the gap is reported
// rather than asserted, because what the rules miss is exactly what the
// combination layer has to case-split on.

// Requires a slack row `s = x - y` to already exist.
//
// push/pop restores BOUNDS ONLY -- not the assignment and not the basis (see
// LraTableau's class comment on bounds-only backtracking; pivots are trailed
// only under PivotingRule::Soi). Verified: on the s = x - y tableau the probe
// moves the slack out of the basis and x into it. So the tableau is left
// bound-equivalent but with an arbitrary basis, and since entailed_equalities
// is basis-DEPENDENT -- which is the whole point of
// fixed_elimination_closes_the_eqrow_basis_gap below -- a caller must not
// assume a later query sees the basis it started with.
bool equality_is_entailed(LraTableau& tableau, LraVarId slack, int probe_lit) {
  const DeltaRational positive{Rational(0), Rational(1)};   // s >= +delta, i.e. s > 0
  const DeltaRational negative{Rational(0), Rational(-1)};  // s <= -delta, i.e. s < 0

  const std::size_t mark_hi = tableau.push();
  bool cannot_be_positive = tableau.assert_lower(slack, positive, Lit{probe_lit}).has_value();
  if (!cannot_be_positive) {
    cannot_be_positive = tableau.check().has_value();
  }
  tableau.pop(mark_hi);

  const std::size_t mark_lo = tableau.push();
  bool cannot_be_negative = tableau.assert_upper(slack, negative, Lit{probe_lit + 1}).has_value();
  if (!cannot_be_negative) {
    cannot_be_negative = tableau.check().has_value();
  }
  tableau.pop(mark_lo);

  return cannot_be_positive && cannot_be_negative;
}

// Every equality the cheap rules report must be genuinely entailed, on a
// handful of deliberately varied shapes. Also counts what the rules miss, so
// the completeness gap is a number rather than an assumption.
void entailed_equalities_agree_with_the_entailment_oracle() {
  std::size_t reported_total = 0;
  std::size_t missed_total = 0;

  // Each case builds x, y, two helper variables, and the slack s = x - y used
  // only by the oracle. `setup` applies the bounds that make the case.
  const auto run_case =
      [&](const std::function<void(LraTableau&, LraVarId, LraVarId, LraVarId, LraVarId)>& setup) {
        LraTableau tableau(LraTableau::PivotingRule::Sparse);
        const LraVarId x = tableau.add_variable();
        const LraVarId y = tableau.add_variable();
        const LraVarId d = tableau.add_variable();
        const LraVarId slack = tableau.add_variable();
        tableau.add_row(slack, {{x, Rational(1)}, {y, Rational(-1)}});
        setup(tableau, x, y, d, slack);
        assert(!tableau.check().has_value());  // cases are satisfiable by construction

        // Each case has exactly one candidate pair, {x, y}, so the single slack
        // s = x - y is the right oracle for whatever comes back.
        const auto reported = tableau.entailed_equalities({x, y});
        for (const auto& eq : reported) {
          assert((eq.lhs == x && eq.rhs == y) || (eq.lhs == y && eq.rhs == x));
          // SOUNDNESS: anything reported must survive the independent oracle.
          assert(equality_is_entailed(tableau, slack, 900));
        }
        reported_total += reported.size();
        if (reported.empty() && equality_is_entailed(tableau, slack, 950)) {
          ++missed_total;  // entailed but not deduced -- the completeness gap
        }
      };

  // 1. Both pinned to the same value: FixedEq should fire.
  run_case([](LraTableau& t, LraVarId x, LraVarId y, LraVarId, LraVarId) {
    assert(!t.assert_lower(x, rat(3), Lit{1}).has_value());
    assert(!t.assert_upper(x, rat(3), Lit{2}).has_value());
    assert(!t.assert_lower(y, rat(3), Lit{3}).has_value());
    assert(!t.assert_upper(y, rat(3), Lit{4}).has_value());
  });
  // 2. Pinned to different values: nothing entailed, nothing reported.
  run_case([](LraTableau& t, LraVarId x, LraVarId y, LraVarId, LraVarId) {
    assert(!t.assert_lower(x, rat(3), Lit{1}).has_value());
    assert(!t.assert_upper(x, rat(3), Lit{2}).has_value());
    assert(!t.assert_lower(y, rat(8), Lit{3}).has_value());
    assert(!t.assert_upper(y, rat(8), Lit{4}).has_value());
  });
  // 3. Only one side pinned: not entailed.
  run_case([](LraTableau& t, LraVarId x, LraVarId y, LraVarId, LraVarId) {
    assert(!t.assert_lower(x, rat(3), Lit{1}).has_value());
    assert(!t.assert_upper(x, rat(3), Lit{2}).has_value());
    assert(!t.assert_lower(y, rat(0), Lit{3}).has_value());
  });
  // 4. THE COMPLETENESS GAP, made concrete. Pinning the slack s = x - y to 0
  //    entails x = y, but neither rule finds it *in this basis*: FixedEq needs
  //    x and y themselves fixed (they are not -- only their difference is),
  //    and EqRow only inspects rows of shared BASIC variables, while here the
  //    basic variable is the slack, which is not shared.
  //
  //    The gap is therefore a property of the BASIS, not of the constraints --
  //    see fixed_elimination_closes_the_eqrow_basis_gap below, which shows the
  //    same constraints yielding the equality once the fixed slack is pivoted
  //    out of the basis.
  run_case([](LraTableau& t, LraVarId, LraVarId, LraVarId, LraVarId slack) {
    assert(!t.assert_lower(slack, rat(0), Lit{11}).has_value());
    assert(!t.assert_upper(slack, rat(0), Lit{12}).has_value());
  });
  // 5. Unconstrained: not entailed.
  run_case([](LraTableau&, LraVarId, LraVarId, LraVarId, LraVarId) {});

  // The rules fired where they should have, and nothing unsound came back.
  assert(reported_total >= 1);
  // And case 4 is genuinely missed -- if this ever becomes 0, either a rule got
  // stronger (good: update this test) or the oracle stopped working (bad).
  assert(missed_total == 1);
}

// The missed case above is missed because of WHERE the fixed variable sits, not
// because the constraints hide the equality. This is the paper's own remark in
// section 5.1 -- "We also aggressively remove fixed variables from the basis" --
// demonstrated rather than quoted.
//
// Same constraints both times: s = x - y with s pinned to 0, so x = y is
// entailed and x, y are individually free. With the fixed slack left in the
// basis, EqRow has no shared basic variable to inspect and finds nothing. Pivot
// it out and the row becomes x = y + s with s fixed at 0, which is exactly
// EqRow's premise.
//
// The practical consequence is recorded in docs/qf-lra-equality-sharing.md:
// --lra-fixed-elimination is OFF by default, and it was measured off on grounds
// that had nothing downstream consuming fixedness. Under theory combination it
// is not merely a pivoting economy -- it decides what we can deduce at all.
void fixed_elimination_closes_the_eqrow_basis_gap() {
  const auto equalities_found = [](bool fixed_elimination) {
    LraTableau tableau(LraTableau::PivotingRule::Sparse);
    tableau.set_fixed_elimination(fixed_elimination);
    const LraVarId x = tableau.add_variable();
    const LraVarId y = tableau.add_variable();
    const LraVarId slack = tableau.add_variable();
    tableau.add_row(slack, {{x, Rational(1)}, {y, Rational(-1)}});
    assert(!tableau.assert_lower(slack, rat(0), Lit{1}).has_value());
    assert(!tableau.assert_upper(slack, rat(0), Lit{2}).has_value());
    assert(!tableau.check().has_value());
    return tableau.entailed_equalities({x, y}).size();
  };
  assert(equalities_found(false) == 0);  // fixed slack stuck in the basis
  assert(equalities_found(true) == 1);   // pivoted out, EqRow fires
}

// The oracle itself must be trustworthy, so pin it on cases with a known
// answer -- a test whose oracle is untested proves nothing.
void the_entailment_oracle_is_itself_correct() {
  LraTableau tableau(LraTableau::PivotingRule::Sparse);
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId slack = tableau.add_variable();
  tableau.add_row(slack, {{x, Rational(1)}, {y, Rational(-1)}});
  assert(!tableau.check().has_value());

  // Unconstrained: x and y are free, so equality is NOT entailed.
  assert(!equality_is_entailed(tableau, slack, 100));

  // Pin both to 7: now it is.
  assert(!tableau.assert_lower(x, rat(7), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(7), Lit{2}).has_value());
  assert(!tableau.assert_lower(y, rat(7), Lit{3}).has_value());
  assert(!tableau.assert_upper(y, rat(7), Lit{4}).has_value());
  assert(!tableau.check().has_value());
  assert(equality_is_entailed(tableau, slack, 200));

  // The oracle must leave the tableau usable -- bound-equivalent, so the same
  // queries still answer, even though the basis may have moved under it.
  assert(!tableau.check().has_value());
  assert(equality_is_entailed(tableau, slack, 300));
}

}  // namespace

// -- M-Mutate: freedom intervals (paper Section 5.2) ------------------------

void freedom_interval_is_the_variables_own_bounds_without_rows() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  assert(!tableau.assert_lower(x, rat(-3), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(7), Lit{2}).has_value());
  assert(!tableau.check().has_value());

  // With no row mentioning x, {l_j} and {u_j} are the only members of the
  // paper's max/min sets, so the interval is exactly x's own bounds.
  const auto interval = tableau.freedom_interval(x);
  assert(interval.lower.has_value() && *interval.lower == rat(-3));
  assert(interval.upper.has_value() && *interval.upper == rat(7));
  assert(interval.contains(rat(0)));
  assert(!interval.contains(rat(8)));
  assert(!interval.contains(rat(-4)));
}

void freedom_interval_is_unbounded_when_nothing_constrains_it() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}, {y, Rational(1)}});
  assert(!tableau.check().has_value());

  // x has no bounds and the only row it feeds has none either, so neither
  // endpoint exists. nullopt has to mean "infinite" rather than "zero" -- a
  // zero-width interval here would silently disable mutation everywhere.
  const auto interval = tableau.freedom_interval(x);
  assert(!interval.lower.has_value());
  assert(!interval.upper.has_value());
  assert(interval.contains(rat(1000000)));
  assert(interval.contains(rat(-1000000)));
}

void freedom_interval_tightens_to_a_basic_variables_bound() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}, {y, Rational(1)}});
  // Both bounds straddle the initial assignment, so check() repairs nothing
  // and the basis stays as add_row() built it. That is load-bearing: with a
  // lower bound of 2 instead, check() pivots s out of the basis and y into
  // it, x's only remaining row is unbounded, and the interval is correctly
  // infinite -- a test written against the pre-check() basis would be
  // asserting something the tableau never promised.
  assert(!tableau.assert_upper(s, rat(10), Lit{1}).has_value());
  assert(!tableau.assert_lower(s, rat(-2), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  assert(tableau.is_basic(s) && !tableau.is_basic(x));

  // beta(s) = beta(x) + beta(y). Moving x by d moves s by d, so x is free
  // exactly as far as s is from each of its own bounds.
  const auto interval = tableau.freedom_interval(x);
  assert(interval.lower.has_value() && interval.upper.has_value());
  const DeltaRational slack_below = tableau.assignment(s) - rat(-2);
  const DeltaRational slack_above = rat(10) - tableau.assignment(s);
  assert(*interval.lower == tableau.assignment(x) - slack_below);
  assert(*interval.upper == tableau.assignment(x) + slack_above);
  assert(update_would_stay_feasible(tableau, x, *interval.lower));
  assert(update_would_stay_feasible(tableau, x, *interval.upper));
}

void freedom_interval_flips_direction_on_a_negative_coefficient() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}, {y, Rational(-1)}});
  assert(!tableau.assert_upper(s, rat(4), Lit{1}).has_value());
  assert(!tableau.check().has_value());

  // s = x - y with only an UPPER bound on s. Raising x raises s, so x gets an
  // upper endpoint and no lower one. y has coefficient -1, so raising y
  // *lowers* s -- y is limited from below instead. Getting this sign case
  // backwards is the likeliest way to write an unsound interval, so both
  // directions are pinned here.
  const auto x_interval = tableau.freedom_interval(x);
  assert(x_interval.upper.has_value() && !x_interval.lower.has_value());
  const auto y_interval = tableau.freedom_interval(y);
  assert(y_interval.lower.has_value() && !y_interval.upper.has_value());

  assert(update_would_stay_feasible(tableau, x, *x_interval.upper));
  assert(update_would_stay_feasible(tableau, y, *y_interval.lower));
  // One step beyond each endpoint must break feasibility, or the endpoint is
  // not actually the boundary.
  assert(!update_would_stay_feasible(tableau, x, *x_interval.upper + rat(1)));
  assert(!update_would_stay_feasible(tableau, y, *y_interval.lower - rat(1)));
}

void freedom_interval_keeps_the_delta_component_of_a_strict_bound() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}});
  // s < 5, encoded as s <= 5 - delta.
  const DeltaRational strict_upper{Rational(5), Rational(-1)};
  assert(!tableau.assert_upper(s, strict_upper, Lit{1}).has_value());
  assert(!tableau.check().has_value());

  // The endpoint must carry the infinitesimal through. Truncating it to the
  // rational part would place x exactly at 5, which violates the strict bound
  // in every concrete model the delta encoding stands for.
  const auto interval = tableau.freedom_interval(x);
  assert(interval.upper.has_value());
  assert(interval.upper->infinitesimal == Rational(-1));
  assert(*interval.upper < rat(5));
  assert(update_would_stay_feasible(tableau, x, *interval.upper));
  assert(!update_would_stay_feasible(tableau, x, rat(5)));
}

void freedom_interval_takes_the_tightest_of_several_rows() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId loose = tableau.add_variable();
  const LraVarId tight = tableau.add_variable();
  tableau.add_row(loose, {{x, Rational(1)}, {y, Rational(1)}});
  tableau.add_row(tight, {{x, Rational(1)}});
  assert(!tableau.assert_upper(loose, rat(100), Lit{1}).has_value());
  assert(!tableau.assert_upper(tight, rat(3), Lit{2}).has_value());
  assert(!tableau.check().has_value());

  // x occurs in two rows; the interval is the intersection, so the tighter
  // row wins. A `min` written as a `max` would pass every single-row test.
  const auto interval = tableau.freedom_interval(x);
  assert(interval.upper.has_value() && *interval.upper == rat(3));
  assert(update_would_stay_feasible(tableau, x, rat(3)));
  assert(!update_would_stay_feasible(tableau, x, rat(4)));
}

void every_value_in_the_freedom_interval_keeps_the_model_feasible() {
  // The paper's actual claim: "if beta satisfies all rows and bound
  // constraints, then beta will also satisfy them after executing
  // update(x_j, v) for any value v in the interval". Assert it directly
  // rather than trusting the formula, over every nonbasic variable of a
  // tableau with mixed signs, several rows and both bound directions.
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId z = tableau.add_variable();
  const LraVarId p = tableau.add_variable();
  const LraVarId q = tableau.add_variable();
  tableau.add_row(p, {{x, Rational(2)}, {y, Rational(-1)}});
  tableau.add_row(q, {{y, Rational(1)}, {z, Rational(3)}});
  assert(!tableau.assert_lower(p, rat(-6), Lit{1}).has_value());
  assert(!tableau.assert_upper(p, rat(6), Lit{2}).has_value());
  assert(!tableau.assert_lower(q, rat(-9), Lit{3}).has_value());
  assert(!tableau.assert_upper(q, rat(9), Lit{4}).has_value());
  assert(!tableau.assert_lower(x, rat(-4), Lit{5}).has_value());
  assert(!tableau.assert_upper(x, rat(4), Lit{6}).has_value());
  assert(!tableau.check().has_value());

  std::size_t checked = 0;
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) {
    if (tableau.is_basic(var)) {
      continue;
    }
    const auto interval = tableau.freedom_interval(var);
    // The current value is always inside its own freedom interval -- the
    // tableau is feasible, so a zero-length move is trivially legal.
    assert(interval.contains(tableau.assignment(var)));
    assert(update_would_stay_feasible(tableau, var, tableau.assignment(var)));

    if (interval.lower.has_value()) {
      assert(update_would_stay_feasible(tableau, var, *interval.lower));
    }
    if (interval.upper.has_value()) {
      assert(update_would_stay_feasible(tableau, var, *interval.upper));
    }
    if (interval.lower.has_value() && interval.upper.has_value()) {
      assert(*interval.lower <= *interval.upper);
      assert(update_would_stay_feasible(tableau, var, midpoint(*interval.lower, *interval.upper)));
    }
    ++checked;
  }
  assert(checked > 0);  // a vacuous loop would pass everything above
}

// -- M-Mutate: the greedy diversifier ---------------------------------------

void diversify_separates_a_coincidence_that_is_not_entailed() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  assert(!tableau.check().has_value());
  // Both sit at 0 with nothing forcing them together: a model coincidence,
  // which is exactly the case the paper wants mutated away rather than turned
  // into a case split.
  assert(tableau.assignment(x) == tableau.assignment(y));
  assert(tableau.model_equal_classes({x, y}).size() == 1);

  const auto result = tableau.diversify_shared_model({x, y});
  assert(result.classes_before == 1);
  assert(result.classes_after == 2);
  assert(result.classes.empty());  // nothing coincides any more
  assert(result.updates.size() == 1);
}

void diversify_cannot_separate_an_entailed_equality() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}, {y, Rational(-1)}});
  // s = x - y pinned to 0, so x = y in every model. No freedom interval can
  // break this, and the mutator must not pretend otherwise -- claiming a
  // separation here would let the combination layer drop a real equality.
  assert(!tableau.assert_lower(s, rat(0), Lit{1}).has_value());
  assert(!tableau.assert_upper(s, rat(0), Lit{2}).has_value());
  assert(!tableau.check().has_value());

  const auto result = tableau.diversify_shared_model({x, y});
  assert(result.classes_before == 1);
  assert(result.classes_after == 1);
  assert(result.updates.empty());
  assert(result.classes.size() == 1 && result.classes.front().size() == 2);
}

void diversify_leaves_the_tableau_untouched() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(1)}, {y, Rational(1)}});
  assert(!tableau.assert_upper(s, rat(8), Lit{1}).has_value());
  assert(!tableau.assert_lower(s, rat(-8), Lit{2}).has_value());
  assert(!tableau.check().has_value());

  std::vector<DeltaRational> before;
  std::vector<bool> basic_before;
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) {
    before.push_back(tableau.assignment(var));
    basic_before.push_back(tableau.is_basic(var));
  }

  const auto result = tableau.diversify_shared_model({x, y});
  assert(result.classes_after >= result.classes_before);

  // The whole design rests on this: the sweep runs on a scratch copy, so a
  // caller can poll it at a check point without the tableau drifting under
  // them. Asserting it rather than trusting the comment, because a claim of
  // exactly this shape was wrong once already in this file's history.
  for (LraVarId var = 0; var < tableau.variable_count(); ++var) {
    assert(tableau.assignment(var) == before[var]);
    assert(tableau.is_basic(var) == basic_before[var]);
  }
  assert_tableau_invariants(tableau);
}

void diversify_is_a_no_op_when_nothing_coincides() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  assert(!tableau.assert_lower(x, rat(5), Lit{1}).has_value());
  assert(!tableau.assert_upper(x, rat(5), Lit{2}).has_value());
  assert(!tableau.check().has_value());
  assert(tableau.assignment(x) != tableau.assignment(y));

  const auto result = tableau.diversify_shared_model({x, y});
  assert(result.classes_before == 2);
  assert(result.classes_after == 2);
  assert(result.updates.empty());  // no coincidence, so no work
  assert(result.classes.empty());
  assert(tableau.diversify_shared_model({}).updates.empty());
}

void diversify_separations_agree_with_the_entailment_oracle() {
  // The differential check. Anything the mutator separates must be an
  // equality the independent oracle agrees is NOT entailed -- separating a
  // genuinely entailed pair would hand the combination layer a disequality
  // that holds in no model.
  const auto run_case =
      [](const std::function<void(LraTableau&, LraVarId, LraVarId, LraVarId)>& setup) {
        LraTableau tableau;
        const LraVarId x = tableau.add_variable();
        const LraVarId y = tableau.add_variable();
        const LraVarId slack = tableau.add_variable();
        tableau.add_row(slack, {{x, Rational(1)}, {y, Rational(-1)}});
        setup(tableau, x, y, slack);
        assert(!tableau.check().has_value());

        const auto before = tableau.model_equal_classes({x, y});
        const auto result = tableau.diversify_shared_model({x, y});
        assert(result.classes_after >= result.classes_before);  // the paper's obligation

        const bool was_coincident = !before.empty();
        const bool separated = was_coincident && result.classes.empty();
        if (separated) {
          assert(!equality_is_entailed(tableau, slack, 700));
        }
        // And the converse direction: an entailed equality can never be separated.
        if (equality_is_entailed(tableau, slack, 701)) {
          assert(!separated);
        }
      };

  run_case([](LraTableau&, LraVarId, LraVarId, LraVarId) {});  // free, coincident at 0
  run_case([](LraTableau& t, LraVarId, LraVarId, LraVarId slack) {
    assert(!t.assert_lower(slack, rat(0), Lit{11}).has_value());
    assert(!t.assert_upper(slack, rat(0), Lit{12}).has_value());  // entailed
  });
  run_case([](LraTableau& t, LraVarId x, LraVarId y, LraVarId) {
    assert(!t.assert_lower(x, rat(3), Lit{13}).has_value());
    assert(!t.assert_upper(x, rat(3), Lit{14}).has_value());
    assert(!t.assert_lower(y, rat(3), Lit{15}).has_value());
    assert(!t.assert_upper(y, rat(3), Lit{16}).has_value());  // both fixed, entailed
  });
  run_case([](LraTableau& t, LraVarId x, LraVarId, LraVarId) {
    assert(!t.assert_lower(x, rat(0), Lit{17}).has_value());
    assert(!t.assert_upper(x, rat(4), Lit{18}).has_value());  // room to move
  });
  run_case([](LraTableau& t, LraVarId, LraVarId, LraVarId slack) {
    assert(!t.assert_upper(slack, rat(0), Lit{19}).has_value());  // x <= y only
  });
}

void diversify_prioritizes_forbidden_equalities_and_returns_the_model() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId z = tableau.add_variable();
  const LraVarId sum = tableau.add_variable();
  tableau.add_row(sum, {{x, Rational(1)}, {y, Rational(1)}, {z, Rational(1)}});
  assert(!tableau.check().has_value());

  const auto result = tableau.diversify_shared_model({x, y, z}, {{x, y}, {y, z}});
  assert(result.values.size() == tableau.variable_count());
  assert(result.forbidden_equalities_before == 2);
  assert(result.forbidden_equalities_after == 0);
  assert(result.value(x) != result.value(y));
  assert(result.value(y) != result.value(z));
  assert(result.value(sum) == result.value(x) + result.value(y) + result.value(z));
  assert(tableau.assignment(x) == DeltaRational{});
  assert(tableau.assignment(y) == DeltaRational{});
  assert(tableau.assignment(z) == DeltaRational{});
}

void diversify_does_not_reintroduce_a_satisfied_forbidden_equality() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId z = tableau.add_variable();
  assert(!tableau.assert_lower(z, rat(1), Lit{51}).has_value());
  assert(!tableau.assert_upper(z, rat(1), Lit{52}).has_value());
  assert(!tableau.check().has_value());

  const auto result = tableau.diversify_shared_model({x, y, z}, {{x, y}, {x, z}});
  assert(result.forbidden_equalities_before == 1);
  assert(result.forbidden_equalities_after == 0);
  assert(result.value(x) != result.value(y));
  assert(result.value(x) != result.value(z));
}

void diversify_reports_an_unresolved_entailed_forbidden_equality() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId difference = tableau.add_variable();
  tableau.add_row(difference, {{x, Rational(1)}, {y, Rational(-1)}});
  assert(!tableau.assert_lower(difference, rat(0), Lit{61}).has_value());
  assert(!tableau.assert_upper(difference, rat(0), Lit{62}).has_value());
  assert(!tableau.check().has_value());

  const auto result = tableau.diversify_shared_model({x, y}, {{x, y}});
  assert(result.forbidden_equalities_before == 1);
  assert(result.forbidden_equalities_after == 1);
  assert(result.value(x) == result.value(y));
}

// -- SOI conflict minimization strategy -------------------------------------

// Two rows that are each infeasible ON THEIR OWN (disjoint variables, each
// row's lower bound exceeding what its variables can sum to), so either one
// alone certifies the conflict and minimization has a genuine choice between
// them. The rows differ in length AND the longer one has the higher id, which
// is what makes the id and row-support orders disagree -- with the lengths
// aligned to the ids every order would coincide and the test would pass
// without testing anything.
struct SoiMinimizationCase {
  LraTableau tableau{LraTableau::PivotingRule::Soi};
  LraVarId short_row = 0;  // two terms
  LraVarId long_row = 0;   // three terms, and the HIGHER id of the two
  std::vector<Lit> asserted;
};

SoiMinimizationCase build_soi_minimization_case(LraTableau::SoiMinimization strategy,
                                                LraTableau::SoiMinimizationOrder order) {
  SoiMinimizationCase built;
  LraTableau::Tuning tuning;
  tuning.soi_minimization = strategy;
  tuning.soi_minimization_order = order;
  built.tableau.set_tuning(tuning);

  const LraVarId x = built.tableau.add_variable();
  const LraVarId y = built.tableau.add_variable();
  const LraVarId u = built.tableau.add_variable();
  const LraVarId z = built.tableau.add_variable();
  const LraVarId w = built.tableau.add_variable();
  built.short_row = built.tableau.add_variable();
  built.long_row = built.tableau.add_variable();

  built.tableau.add_row(built.short_row, {{z, Rational(1)}, {w, Rational(1)}});
  built.tableau.add_row(built.long_row, {{x, Rational(1)}, {y, Rational(1)}, {u, Rational(1)}});
  for (const LraVarId var : {x, y, u, z, w}) {
    const Lit lower{static_cast<int>(10 + var)};
    const Lit upper{static_cast<int>(20 + var)};
    assert(!built.tableau.assert_lower(var, rat(0), lower).has_value());
    assert(!built.tableau.assert_upper(var, rat(1), upper).has_value());
    built.asserted.push_back(lower);
    built.asserted.push_back(upper);
  }
  // Each row's variables can sum to at most 2 and 3 respectively.
  assert(!built.tableau.assert_lower(built.short_row, rat(10), Lit{7}).has_value());
  assert(!built.tableau.assert_lower(built.long_row, rat(10), Lit{8}).has_value());
  built.asserted.push_back(Lit{7});
  built.asserted.push_back(Lit{8});
  return built;
}

void soi_minimization_none_keeps_every_violated_row() {
  auto built = build_soi_minimization_case(LraTableau::SoiMinimization::None,
                                           LraTableau::SoiMinimizationOrder::Id);
  const auto conflict = built.tableau.check();
  assert(conflict.has_value());
  // Both rows' own bound reasons survive, plus every pinned variable: the
  // ablation baseline is the whole error set, and it is what the other arms
  // have to beat.
  assert(has_reason(*conflict, Lit{7}) && has_reason(*conflict, Lit{8}));
  assert(conflict->size() == 7);
}

void soi_deletion_row_support_keeps_the_shorter_row() {
  auto id_case = build_soi_minimization_case(LraTableau::SoiMinimization::Deletion,
                                             LraTableau::SoiMinimizationOrder::Id);
  const auto id_conflict = id_case.tableau.check();
  assert(id_conflict.has_value());

  auto support_case = build_soi_minimization_case(LraTableau::SoiMinimization::Deletion,
                                                  LraTableau::SoiMinimizationOrder::RowSupport);
  const auto support_conflict = support_case.tableau.check();
  assert(support_conflict.has_value());

  // Deletion drops what it visits first. Ascending id offers the short row
  // first, so the LONG row survives; longest-first offers the long row
  // first, so the SHORT one does.
  assert(has_reason(*id_conflict, Lit{8}) && !has_reason(*id_conflict, Lit{7}));
  assert(has_reason(*support_conflict, Lit{7}) && !has_reason(*support_conflict, Lit{8}));

  // And that is the entire point of the ordering: a shorter surviving row
  // means a shorter clause. If this ever stops holding, the heuristic has no
  // rationale left.
  assert(support_conflict->size() < id_conflict->size());
}

void soi_minimization_arms_all_produce_a_sound_conflict() {
  // Every literal a conflict names must be a reason we actually asserted --
  // an arm that invented or dropped a literal would be unsound regardless of
  // how short its clause is.
  const std::vector<std::pair<LraTableau::SoiMinimization, LraTableau::SoiMinimizationOrder>> arms =
      {{LraTableau::SoiMinimization::QuickXplain, LraTableau::SoiMinimizationOrder::Id},
       {LraTableau::SoiMinimization::Deletion, LraTableau::SoiMinimizationOrder::Id},
       {LraTableau::SoiMinimization::Deletion, LraTableau::SoiMinimizationOrder::RowSupport},
       {LraTableau::SoiMinimization::Deletion, LraTableau::SoiMinimizationOrder::Violation},
       {LraTableau::SoiMinimization::None, LraTableau::SoiMinimizationOrder::Id}};

  for (const auto& [strategy, order] : arms) {
    auto built = build_soi_minimization_case(strategy, order);
    const auto conflict = built.tableau.check();
    assert(conflict.has_value());  // the system is unsat under every arm
    assert(!conflict->empty());
    for (const Lit lit : *conflict) {
      assert(has_reason(built.asserted, lit));
    }
    // Every arm must keep at least one of the two row bounds, or it is not
    // explaining the infeasibility at all.
    assert(has_reason(*conflict, Lit{7}) || has_reason(*conflict, Lit{8}));
  }
}

void soi_minimization_is_deterministic() {
  // Ties break by variable id precisely so a portfolio arm is reproducible;
  // an arm whose clause varies run to run cannot be measured against another.
  for (const auto order :
       {LraTableau::SoiMinimizationOrder::Id, LraTableau::SoiMinimizationOrder::RowSupport,
        LraTableau::SoiMinimizationOrder::Violation}) {
    auto first = build_soi_minimization_case(LraTableau::SoiMinimization::Deletion, order);
    auto second = build_soi_minimization_case(LraTableau::SoiMinimization::Deletion, order);
    const auto first_conflict = first.tableau.check();
    const auto second_conflict = second.tableau.check();
    assert(first_conflict.has_value() && second_conflict.has_value());
    // sat::Lit has no operator==, so compare the underlying values in order.
    assert(first_conflict->size() == second_conflict->size());
    for (std::size_t i = 0; i < first_conflict->size(); ++i) {
      assert((*first_conflict)[i].value == (*second_conflict)[i].value);
    }
  }
}

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
  push_pop_rebuilds_violated_vars_across_multiple_simultaneous_violations();
  pop_restores_bounds_and_keeps_the_assignment_consistent();
  nested_push_pop_levels();
  bound_accessors_report_nullopt_before_any_assertion();
  bound_accessors_report_value_and_reason();
  satisfiable_system_via_a_slack_row_under_dantzig();
  infeasible_system_requires_a_pivot_to_discover_under_dantzig();
  multi_pivot_system_with_varied_coefficients_under_dantzig();
  sparse_picks_the_column_occurring_in_fewest_rows();
  sparse_prefers_a_unit_coefficient_when_occurrences_tie();
  sparse_bounded_pricing_still_finds_the_only_eligible_candidate();
  tuning_knobs_never_change_a_verdict();
  bland_fallback_is_counted_under_the_sparse_rule();
  sparse_matches_bland_on_multi_pivot_scenarios();
  least_violated_leaving_matches_bland_on_multi_pivot_scenarios();
  least_violated_leaving_selects_differently_from_most_violated();
  soi_reproduces_paper_fig6_worked_example();
  soi_zero_breakpoints_moving_inward_do_not_change_the_slope();
  soi_zero_breakpoints_moving_outward_change_the_slope();
  soi_fixed_variable_at_zero_contributes_one_direction_change();
  soi_fixed_variable_at_nonzero_contributes_both_direction_changes();
  soi_matches_bland_on_multi_pivot_scenarios();
  soi_push_pop_restores_state_across_a_pivot();
  soi_multi_row_conflict_includes_every_violated_row();
  soi_quickxplain_minimizes_independent_conflicts();
  fixed_basic_variable_is_pivoted_out_of_the_basis();
  elimination_is_off_unless_enabled();
  elimination_prefers_an_unbounded_entering_candidate();
  elimination_never_pivots_an_already_fixed_variable_into_the_basis();
  conflict_is_still_found_through_an_eliminated_variable();
  push_pop_restores_state_across_an_elimination_pivot();
  elimination_agrees_with_the_ablation_on_multi_pivot_scenarios();
  rows_stay_ordered_and_nonzero_through_pivoting();
  merge_and_sort_rebuilds_agree_on_a_cancelling_pivot();
  delta_maintained_occurs_in_matches_a_full_rebuild();
  cancelling_pivot_drops_the_occurrence_entry();
  model_equal_classes_groups_shared_variables_by_model_value();
  model_equal_classes_ignores_variables_outside_the_shared_set();
  model_equal_classes_separates_values_differing_only_in_delta();
  model_equal_classes_is_empty_when_nothing_coincides();
  model_equal_classes_deduplicates_the_shared_set();
  model_equal_classes_track_the_model_across_assertions();
  fixedeq_reports_variables_pinned_to_the_same_value();
  fixedeq_does_not_report_different_fixed_values();
  entailed_equalities_tolerates_variables_with_one_or_no_bound();
  eqrow_reports_a_bare_defining_row_with_no_reasons();
  eqrow_reports_when_the_remainder_is_fixed_at_zero();
  eqrow_declines_when_the_remainder_is_fixed_nonzero();
  eqrow_declines_when_the_remainder_is_not_fixed();
  eqrow_declines_on_a_negated_coefficient();
  entailed_equalities_respects_the_shared_set();
  entailed_equalities_emits_a_spanning_set_not_all_pairs();
  eqoffsetrows_reports_matching_fixed_offsets();
  eqoffsetrows_rejects_different_offsets();
  eqrows_reports_matching_polynomials_and_emits_a_spanning_set();
  eqrows_rejects_different_nonfixed_polynomials();
  model_unit_eqrow_avoids_large_fixed_offset_construction();
  model_unit_filter_requires_repair_after_an_infeasible_branch();
  model_unit_two_row_accepts_both_same_unit_signs();
  model_unit_two_row_rejects_opposite_signs_and_nonunit_connectors();
  model_unit_output_is_a_deterministic_spanning_set();
  model_unit_entailment_disappears_when_a_fixing_bound_is_popped();
  fixed_elimination_closes_the_eqrow_basis_gap();
  the_entailment_oracle_is_itself_correct();
  entailed_equalities_agree_with_the_entailment_oracle();
  freedom_interval_is_the_variables_own_bounds_without_rows();
  freedom_interval_is_unbounded_when_nothing_constrains_it();
  freedom_interval_tightens_to_a_basic_variables_bound();
  freedom_interval_flips_direction_on_a_negative_coefficient();
  freedom_interval_keeps_the_delta_component_of_a_strict_bound();
  freedom_interval_takes_the_tightest_of_several_rows();
  every_value_in_the_freedom_interval_keeps_the_model_feasible();
  diversify_separates_a_coincidence_that_is_not_entailed();
  diversify_cannot_separate_an_entailed_equality();
  diversify_leaves_the_tableau_untouched();
  diversify_is_a_no_op_when_nothing_coincides();
  diversify_separations_agree_with_the_entailment_oracle();
  diversify_prioritizes_forbidden_equalities_and_returns_the_model();
  diversify_does_not_reintroduce_a_satisfied_forbidden_equality();
  diversify_reports_an_unresolved_entailed_forbidden_equality();
  soi_minimization_none_keeps_every_violated_row();
  soi_deletion_row_support_keeps_the_shorter_row();
  soi_minimization_arms_all_produce_a_sound_conflict();
  soi_minimization_is_deterministic();
  return 0;
}
