#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <vector>

#include "sat/lit.hpp"
#include "theory/arithmetic/lia_branch_and_bound.hpp"
#include "theory/arithmetic/lia_explanation.hpp"
#include "theory/arithmetic/lia_gcd_consistency.hpp"
#include "theory/arithmetic/lia_gomory_cuts.hpp"
#include "theory/arithmetic/lra_tableau.hpp"
#include "util/rational.hpp"

using primo::sat::Lit;
using primo::theory::arithmetic::apply_gomory_cuts;
using primo::theory::arithmetic::branch_and_bound;
using primo::theory::arithmetic::DeltaRational;
using primo::theory::arithmetic::gcd_inconsistent_row;
using primo::theory::arithmetic::kDerivedBoundReason;
using primo::theory::arithmetic::LiaBranchAndBoundResult;
using primo::theory::arithmetic::LiaExplanation;
using primo::theory::arithmetic::LraTableau;
using primo::theory::arithmetic::LraVarId;
using primo::util::Rational;

namespace {

DeltaRational rat(const Rational& value) { return DeltaRational::from_rational(value); }
DeltaRational rat(long value) { return DeltaRational::from_rational(Rational(value)); }

bool contains(const LiaExplanation& explanation, int literal) {
  const auto& lits = explanation.literals();
  return std::any_of(lits.begin(), lits.end(), [literal](Lit lit) { return lit.value == literal; });
}

// The property solve_qf_lia_impl's generalized_lia_lemma() actually relies
// on, asserted directly here rather than only end-to-end: an explanation
// never names a literal that was not one of the reasons fed to the tableau.
// A literal outside that set could not be blocking for the round's Boolean
// assignment, and a lemma containing one would fail to exclude the
// assignment that produced it.
bool only_names(const LiaExplanation& explanation, const std::vector<int>& allowed) {
  for (const Lit lit : explanation.literals()) {
    if (std::find(allowed.begin(), allowed.end(), lit.value) == allowed.end()) {
      return false;
    }
  }
  return true;
}

// kDerivedBoundReason names no SAT atom, so it must never survive into an
// explanation -- the single most important property here, since every
// derived bound in both cut and branch code asserts with it.
void the_synthetic_reason_is_never_recorded() {
  LiaExplanation explanation;
  explanation.add(kDerivedBoundReason);
  assert(explanation.empty());
  explanation.add_all({kDerivedBoundReason, Lit{7}, kDerivedBoundReason});
  assert(explanation.literals().size() == 1);
  assert(contains(explanation, 7));
}

void literals_are_deduplicated_and_sorted() {
  LiaExplanation explanation;
  explanation.add_all({Lit{5}, Lit{-3}, Lit{5}, Lit{9}, Lit{-3}});
  const auto& lits = explanation.literals();
  assert(lits.size() == 3);
  assert(lits[0].value == -3);
  assert(lits[1].value == 5);
  assert(lits[2].value == 9);
}

// A negative literal is an ordinary blocking literal (blocking_lit() emits
// -atom.lit for a true atom), so sign must not be confused with the
// synthetic-reason test, which is against the value 0 alone.
void negative_literals_are_kept() {
  LiaExplanation explanation;
  explanation.add(Lit{-11});
  assert(contains(explanation, -11));
}

// 2x - 2y = 1 (the GCD check's motivating row): the slack is fixed by the
// pair of bounds asserted on it, and those two reasons are exactly what the
// residual is computed from, so both must be recorded -- and nothing else,
// since x and y are free and contribute only coefficients.
void gcd_records_the_bounds_that_fixed_the_row() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(2)}, {y, Rational(-2)}});
  assert(!tableau.assert_upper(s, rat(1), Lit{11}).has_value());
  assert(!tableau.assert_lower(s, rat(1), Lit{12}).has_value());
  assert(!tableau.check().has_value());

  LiaExplanation explanation;
  assert(gcd_inconsistent_row(tableau, {x, y}, &explanation).has_value());
  assert(contains(explanation, 11));
  assert(contains(explanation, 12));
  assert(explanation.literals().size() == 2);
}

// A consistent scan must leave the accumulator untouched: solve_qf_lia_impl
// threads one instance through all three stages, so a stage that finds
// nothing must not contribute literals a later stage's lemma would then
// carry for no reason.
void gcd_records_nothing_when_every_row_is_consistent() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId y = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(2)}, {y, Rational(-3)}});
  assert(!tableau.assert_upper(s, rat(1), Lit{21}).has_value());
  assert(!tableau.assert_lower(s, rat(1), Lit{22}).has_value());
  assert(!tableau.check().has_value());

  LiaExplanation explanation;
  assert(!gcd_inconsistent_row(tableau, {x, y}, &explanation).has_value());
  assert(explanation.empty());
}

// x_b = x1 + x2 with x1, x2 pinned to 1/2 each: x_b = 1 is integral, but the
// cut derived from the fractional intermediate closes the tableau. The cut's
// threshold bound goes in with kDerivedBoundReason, so the resulting conflict
// contains it -- and the explanation must come back naming only the four real
// bound reasons that pinned x1 and x2.
void cut_explanation_names_only_real_bound_reasons() {
  LraTableau tableau;
  const LraVarId x1 = tableau.add_variable();
  const LraVarId x2 = tableau.add_variable();
  const LraVarId x_b = tableau.add_variable();
  tableau.add_row(x_b, {{x1, Rational(2)}, {x2, Rational(3)}});
  assert(!tableau.assert_lower(x1, rat(Rational(0)), Lit{31}).has_value());
  assert(!tableau.assert_upper(x1, rat(Rational(0)), Lit{32}).has_value());
  assert(!tableau.assert_lower(x2, rat(Rational(1, 2)), Lit{33}).has_value());
  assert(!tableau.assert_upper(x2, rat(Rational(1, 2)), Lit{34}).has_value());
  assert(!tableau.check().has_value());
  assert(tableau.assignment(x_b) == rat(Rational(3, 2)));

  LiaExplanation explanation;
  const bool infeasible = apply_gomory_cuts(tableau, {x_b}, 10, &explanation);
  assert(infeasible);
  assert(!explanation.empty());
  assert(!contains(explanation, 0));
  assert(only_names(explanation, {31, 32, 33, 34}));
}

// Antecedents are recorded even when the cut does not itself conflict: the
// cut stays in the tableau for the rest of the round, so anything that
// conflicts on it later has its kDerivedBoundReason dropped and needs these
// literals to already be present.
void cut_antecedents_are_recorded_even_without_a_conflict() {
  LraTableau tableau;
  const LraVarId x1 = tableau.add_variable();
  const LraVarId x2 = tableau.add_variable();
  const LraVarId x_b = tableau.add_variable();
  tableau.add_row(x_b, {{x1, Rational(2)}, {x2, Rational(3)}});
  assert(!tableau.assert_lower(x1, rat(Rational(0)), Lit{41}).has_value());
  assert(!tableau.assert_lower(x2, rat(Rational(1, 2)), Lit{42}).has_value());
  assert(!tableau.check().has_value());

  LiaExplanation explanation;
  const bool infeasible = apply_gomory_cuts(tableau, {x_b}, 10, &explanation);
  assert(!infeasible);
  assert(contains(explanation, 41));
  assert(contains(explanation, 42));
  assert(!contains(explanation, 0));
}

// A cut budget of zero must not fabricate an explanation.
void zero_cut_limit_records_nothing() {
  LraTableau tableau;
  const LraVarId x1 = tableau.add_variable();
  const LraVarId x_b = tableau.add_variable();
  tableau.add_row(x_b, {{x1, Rational(3)}});
  assert(!tableau.assert_lower(x1, rat(Rational(1, 2)), Lit{51}).has_value());
  assert(!tableau.check().has_value());

  LiaExplanation explanation;
  assert(!apply_gomory_cuts(tableau, {x_b}, 0, &explanation));
  assert(explanation.empty());
}

// 2 <= 3x <= 2, i.e. x pinned to 2/3: no integer value exists, and
// branch-and-bound refutes both sides of the split. Both split bounds carry
// kDerivedBoundReason, so this is the branch-side counterpart of the cut
// test -- the tautology argument in lia_explanation.hpp says dropping them
// is sound, and what must come back is exactly the two real bounds.
void branch_and_bound_explanation_names_only_real_bound_reasons() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  const LraVarId s = tableau.add_variable();
  tableau.add_row(s, {{x, Rational(3)}});
  assert(!tableau.assert_lower(s, rat(2), Lit{61}).has_value());
  assert(!tableau.assert_upper(s, rat(2), Lit{62}).has_value());
  assert(!tableau.check().has_value());

  LiaExplanation explanation;
  const LiaBranchAndBoundResult result = branch_and_bound(tableau, {x}, 1000, &explanation);
  assert(result.status == LiaBranchAndBoundResult::Status::Unsat);
  assert(!explanation.empty());
  assert(!contains(explanation, 0));
  assert(only_names(explanation, {61, 62}));
}

// On a Sat outcome the accumulator is explicitly *not* required to be empty:
// x >= 1/2 refutes the `x <= 0` side before succeeding on `x >= 1`, and that
// refutation's conflict is genuinely recorded on the way past. It is
// meaningless rather than wrong -- solve_qf_lia_impl reads the accumulator
// only on the Unsat path -- but the invariant that does have to survive even
// here is that nothing synthetic or invented leaked in, since a later stage
// in the same round shares this accumulator.
void branch_and_bound_may_record_refuted_branches_on_sat() {
  LraTableau tableau;
  const LraVarId x = tableau.add_variable();
  assert(!tableau.assert_lower(x, rat(Rational(1, 2)), Lit{71}).has_value());
  assert(!tableau.check().has_value());

  LiaExplanation explanation;
  const LiaBranchAndBoundResult result = branch_and_bound(tableau, {x}, 1000, &explanation);
  assert(result.status == LiaBranchAndBoundResult::Status::Sat);
  assert(!contains(explanation, 0));
  assert(only_names(explanation, {71}));
}

// Every stage must stay callable without an accumulator -- the default
// argument is what keeps the existing unit tests and any non-QF_LIA caller
// unaffected by this change. Same 3x = 2 row as the branch test above, which
// no integer x satisfies, so each stage is exercised on a case it actually
// refutes rather than on a trivially consistent one. Each gets its own fresh
// tableau: apply_gomory_cuts and branch_and_bound both mutate, and running
// one on the wreckage the other left is not a state either of them documents
// a contract for.
void every_stage_still_works_without_an_explanation() {
  const auto build = [](LraTableau& tableau) {
    const LraVarId x = tableau.add_variable();
    const LraVarId s = tableau.add_variable();
    tableau.add_row(s, {{x, Rational(3)}});
    assert(!tableau.assert_lower(s, rat(2), Lit{81}).has_value());
    assert(!tableau.assert_upper(s, rat(2), Lit{82}).has_value());
    assert(!tableau.check().has_value());
    return x;
  };

  // gcd(3) does not divide the residual 2.
  LraTableau gcd_tableau;
  const LraVarId gcd_x = build(gcd_tableau);
  assert(gcd_inconsistent_row(gcd_tableau, {gcd_x}).has_value());

  // x is pivoted basic at 2/3 with s (fixed, hence at a bound) as its only
  // row term, so the row is cut-eligible and the cut closes the tableau.
  LraTableau cut_tableau;
  const LraVarId cut_x = build(cut_tableau);
  assert(apply_gomory_cuts(cut_tableau, {cut_x}, 10));

  LraTableau branch_tableau;
  const LraVarId branch_x = build(branch_tableau);
  assert(branch_and_bound(branch_tableau, {branch_x}, 1000).status ==
         LiaBranchAndBoundResult::Status::Unsat);
}

}  // namespace

int main() {
  the_synthetic_reason_is_never_recorded();
  literals_are_deduplicated_and_sorted();
  negative_literals_are_kept();
  gcd_records_the_bounds_that_fixed_the_row();
  gcd_records_nothing_when_every_row_is_consistent();
  cut_explanation_names_only_real_bound_reasons();
  cut_antecedents_are_recorded_even_without_a_conflict();
  zero_cut_limit_records_nothing();
  branch_and_bound_explanation_names_only_real_bound_reasons();
  branch_and_bound_may_record_refuted_branches_on_sat();
  every_stage_still_works_without_an_explanation();
  return 0;
}
