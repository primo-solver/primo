#include "theory/arithmetic/lia_gomory_cuts.hpp"

#include <optional>
#include <utility>

#include "sat/lit.hpp"
#include "util/rational.hpp"

// Adding rows/asserting bounds mid-search, without ever undoing them, is
// sound here specifically because of how this function is called: once
// per full integer check through complete_integer_assignment, on a private `lia_tableau`
// copy that is discarded and rebuilt fresh every round regardless (see
// check_sat.cpp). A cut therefore never needs to survive a pop() past its
// own creation point -- it only needs to last for the remainder of the
// one round it was derived in, which LraTableau::add_row() (computes the
// new row's assignment from whatever the current nonbasic values already
// are; touches no trail_ state) followed by assert_upper/assert_lower
// (whose is_basic branch already calls update_violation_status()) and
// then check() already handles correctly -- the same pattern
// prepare_lra_equality_probe_context in check_sat.cpp already uses in
// production. No LraTableau change was needed for this (see
// docs/qf-lia-branch-and-bound.md's stage 3 section for the fuller
// argument); a cut is simply never removed once added, and a cut is not
// carried over into the *next* SAT round either, since that round starts
// from a fresh tableau copy.

namespace primo::theory::arithmetic {

namespace {

// Whether `var` is a non-basic term currently exactly AT one of its
// bounds (King's eligibility precondition, a_j = l_j / a_k = u_k) --
// not merely "has a bound": LraTableau's own documented deviation is
// that a non-basic variable may sit strictly *within* its bounds, and a
// term doing that disqualifies the whole row for this cut (see
// find_eligible_row). No integrality requirement on the bound value
// itself -- that is the whole point of using King's mixed-Gomory-cut
// formula here rather than a pure-integer one (see the .hpp comment).
struct BoundSite {
  util::Rational value;
  bool is_lower;
  // The bound this site sits at is exactly what the derived cut rests on,
  // so its reason is one of the cut's antecedents -- collected by
  // apply_gomory_cuts into the round's LiaExplanation, which is what lets
  // the cut's own synthetic kDerivedBoundReason be dropped from any
  // conflict the cut later takes part in.
  sat::Lit reason;
};

std::optional<BoundSite> current_bound_site(const LraTableau& tableau, LraVarId var) {
  const DeltaRational& value = tableau.assignment(var);
  if (const auto lower = tableau.lower_bound(var); lower.has_value() && lower->value == value) {
    return BoundSite{lower->value.rational, true, lower->reason};
  }
  if (const auto upper = tableau.upper_bound(var); upper.has_value() && upper->value == value) {
    return BoundSite{upper->value.rational, false, upper->reason};
  }
  return std::nullopt;
}

struct EligibleRow {
  LraVarId basic_var = 0;
  std::vector<std::pair<LraTableau::RowEntry, BoundSite>> terms;
};

// Scans `integer_vars` -- the declared Int problem variables, exactly
// what branch_and_bound's own find_fractional_variable already scans --
// not slacks: a slack's row can carry non-integer coefficients
// (PreparedLraTheory's scale-canonicalization), so a slack is not
// guaranteed integer-valued in every model the way a declared Int
// variable is, and King's theorem needs that guarantee for the row's
// basic variable specifically. Returns the first basic, currently
// fractional variable whose entire row is cut-eligible (every term at a
// bound), or nullopt if none qualifies -- a row with even one term
// sitting strictly between its bounds is skipped entirely, not partially
// cut.
//
// A basic variable whose *value* carries a nonzero infinitesimal is
// excluded from eligibility entirely, not just treated as "fractional"
// the way find_fractional_variable does: King's formula computes
// `f0 = a0 - floor(a0)` from the value's rational part alone, and a
// declared Int variable can pick up a nonzero infinitesimal purely by
// being basic in a row whose other terms carry one (e.g. a slack
// currently sitting at a strict, delta-trick bound -- see
// bound_assertion() in check_sat.cpp), with no delta of its own. When
// that value's rational part happens to already be an integer (the
// infinitesimal is the only thing making it not-yet-a-clean-integer),
// `f0` computes to exactly 0 and every J-/K+ term's `entry.coeff / f0`
// below divides by zero. branch_and_bound handles this shape correctly
// on its own (its own floor-based branching does not depend on f0 being
// nonzero), so skipping it here -- falling back to that path -- costs
// nothing but a missed cut, never soundness or a crash.
std::optional<EligibleRow> find_eligible_row(const LraTableau& tableau,
                                             const std::vector<LraVarId>& integer_vars) {
  for (const LraVarId var : integer_vars) {
    if (!tableau.is_basic(var)) {
      continue;
    }
    const DeltaRational& value = tableau.assignment(var);
    if (!value.infinitesimal.is_zero()) {
      continue;
    }
    if (value.rational.is_integer()) {
      continue;
    }
    EligibleRow candidate;
    candidate.basic_var = var;
    bool all_eligible = true;
    for (const auto& entry : tableau.row(var)) {
      const auto site = current_bound_site(tableau, entry.var);
      if (!site.has_value()) {
        all_eligible = false;
        break;
      }
      candidate.terms.emplace_back(entry, *site);
    }
    // An empty-but-eligible row cannot happen: LraTableau::add_row gives
    // an empty row assignment exactly 0 (integer), which the fractional
    // check above already excludes.
    if (all_eligible) {
      return candidate;
    }
  }
  return std::nullopt;
}

// Builds and applies King's thesis eq. 4.9 mixed Gomory cut for `row`
// (whose basic variable's current value is `a0`, fractional by
// find_eligible_row's own precondition): a new tableau row (the cut's
// slack) plus a lower bound on it. Per-term coefficient and the overall
// threshold are both derived by expanding `sum C_term*(x_term -
// bound_term) >= 1` and moving every `bound_term` constant onto the
// right-hand side -- see docs/qf-lia-branch-and-bound.md's stage 3
// section for the full algebra and two independently hand-verified
// worked examples this matches term-for-term.
//
// f0 = frac(a0) is always in (0,1) here (a0 fractional by construction),
// so both f0 and 1-f0 are safe, nonzero divisors.
std::optional<std::vector<sat::Lit>> apply_one_cut(LraTableau& tableau, const EligibleRow& row,
                                                   const util::Rational& a0) {
  const util::Rational f0 = a0 - a0.floor();
  const util::Rational one_minus_f0 = util::Rational(1) - f0;

  std::vector<std::pair<LraVarId, util::Rational>> cut_terms;
  cut_terms.reserve(row.terms.size());
  util::Rational threshold(1);
  for (const auto& [entry, site] : row.terms) {
    // J+ (lower bound, coeff>0) and K- (upper bound, coeff<0) divide by
    // (1-f0); J- and K+ divide by f0 -- see the .hpp comment and the
    // docs section for the derivation these four cases come from.
    const bool same_sign = site.is_lower ? entry.coeff.sign() > 0 : entry.coeff.sign() < 0;
    const util::Rational coefficient = same_sign ? entry.coeff / one_minus_f0 : -entry.coeff / f0;
    cut_terms.emplace_back(entry.var, coefficient);
    threshold += coefficient * site.value;
  }

  const LraVarId cut_slack = tableau.add_variable();
  tableau.add_row(cut_slack, cut_terms);
  const auto assert_conflict =
      tableau.assert_lower(cut_slack, DeltaRational::from_rational(threshold), kDerivedBoundReason);
  if (assert_conflict.has_value()) {
    return assert_conflict;
  }
  return tableau.check();
}

}  // namespace

bool apply_gomory_cuts(LraTableau& tableau, const std::vector<LraVarId>& integer_vars,
                       std::size_t cut_limit, LiaExplanation* explanation) {
  for (std::size_t iteration = 0; iteration < cut_limit; ++iteration) {
    const auto eligible = find_eligible_row(tableau, integer_vars);
    if (!eligible.has_value()) {
      return false;
    }
    const util::Rational a0 = tableau.assignment(eligible->basic_var).rational;
    if (explanation != nullptr) {
      // Record the cut's antecedents *before* checking whether it closes the
      // tableau, and unconditionally: a cut that does not conflict on its own
      // still stays in the tableau for the rest of the round, so a later cut
      // -- or branch-and-bound afterwards -- can produce a conflict resting on
      // it. Its kDerivedBoundReason is dropped from that conflict, so these
      // antecedents have to already be in the explanation by then.
      for (const auto& [entry, site] : eligible->terms) {
        explanation->add(site.reason);
      }
    }
    const auto cut_conflict = apply_one_cut(tableau, *eligible, a0);
    if (cut_conflict.has_value()) {
      if (explanation != nullptr) {
        explanation->add_all(*cut_conflict);
      }
      return true;
    }
  }
  return false;
}

}  // namespace primo::theory::arithmetic
