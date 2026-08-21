#pragma once

#include <vector>

#include "sat/lit.hpp"
#include "theory/arithmetic/lra_tableau.hpp"

namespace primo::theory::arithmetic {

// The synthetic reason QF_LIA's own *derived* bounds are asserted with: a
// Gomory cut's threshold (lia_gomory_cuts.cpp) and branch-and-bound's
// split bounds (lia_branch_and_bound.cpp) come from the integer reasoning
// itself rather than from any SAT atom, so there is no literal that names
// them. LraTableau copies a bound's reason straight into the conflicts it
// returns (build_conflict, and assert_upper/assert_lower's own two-literal
// bound-vs-bound conflict), so this value does reach explanations, where it
// has to be dropped and replaced by the derived bound's antecedents -- see
// LiaExplanation.
inline constexpr sat::Lit kDerivedBoundReason = sat::Lit{0};

// The blocking literals of one round's integer-infeasibility proof,
// accumulated across every stage that contributed to it: GCD-consistency,
// Gomory cuts, and branch-and-bound all feed the same instance.
//
// Literals follow the convention LraTableau documents on its own conflict
// vectors -- already negated ("blocking"), so the accumulated set is
// directly usable as a SAT clause with no translation. Duplicates collapse,
// and kDerivedBoundReason is dropped on the way in since it names no atom.
//
// Dropping it is sound only because each stage that asserts a derived bound
// also supplies what that bound rests on:
//
//   * A Gomory cut is implied by the bounds its row's terms currently sit
//     at, so those bounds' reasons are the cut's antecedents and go in
//     alongside. The row equation the cut is read off is a tableau
//     invariant, not an assumption, and contributes nothing.
//   * A branch-and-bound split needs no antecedents at all: `x <= n` or
//     `x >= n+1` is a tautology over the integers, so once both sides are
//     refuted, the union of the two sides' explanations already refutes the
//     parent without mentioning the split.
//
// A *superset* of the true explanation is always sound here -- it yields a
// weaker lemma, never an invalid one. A set containing a literal that is not
// blocking for the current assignment is not sound, and solve_qf_lia_impl
// therefore validates containment before using the result rather than
// trusting these accumulation rules; see generalized_lia_lemma().
class LiaExplanation {
 public:
  // Ignores kDerivedBoundReason; keeps `literals()` sorted and duplicate-free.
  void add(sat::Lit lit);
  void add_all(const std::vector<sat::Lit>& lits);

  // Both of `var`'s bound reasons, i.e. why it is currently *fixed*. Used by
  // the GCD-consistency check, whose argument folds fixed variables into a
  // residual constant and so depends on both directions.
  void add_fixed_bound_reasons(const LraTableau& tableau, LraVarId var);

  [[nodiscard]] const std::vector<sat::Lit>& literals() const { return literals_; }
  [[nodiscard]] bool empty() const { return literals_.empty(); }

 private:
  std::vector<sat::Lit> literals_;
};

}  // namespace primo::theory::arithmetic
