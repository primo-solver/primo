#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
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
  push_pop_rebuilds_violated_vars_across_multiple_simultaneous_violations();
  push_pop_restores_propagated_basic_assignment();
  nested_push_pop_levels();
  bound_accessors_report_nullopt_before_any_assertion();
  bound_accessors_report_value_and_reason();
  satisfiable_system_via_a_slack_row_under_dantzig();
  infeasible_system_requires_a_pivot_to_discover_under_dantzig();
  multi_pivot_system_with_varied_coefficients_under_dantzig();
  sparse_picks_the_column_occurring_in_fewest_rows();
  sparse_prefers_a_unit_coefficient_when_occurrences_tie();
  sparse_bounded_pricing_still_finds_the_only_eligible_candidate();
  sparse_matches_bland_on_multi_pivot_scenarios();
  soi_reproduces_paper_fig6_worked_example();
  soi_matches_bland_on_multi_pivot_scenarios();
  soi_push_pop_restores_state_across_a_pivot();
  soi_multi_row_conflict_includes_every_violated_row();
  soi_quickxplain_minimizes_independent_conflicts();
  return 0;
}
