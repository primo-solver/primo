#pragma once

#include <cstddef>
#include <vector>

#include "theory/arithmetic/lra_tableau.hpp"

namespace primo::theory::arithmetic {

// QF_LIA Gomory cuts (stage 3, docs/qf-lia-branch-and-bound.md): King's
// thesis (NYU 2014), Sect. 4.4.3, Lemma 4.1 / eq. 4.9, "Mixed Gomory
// Cuts" -- chosen over a self-derived pure-integer variant because it
// only requires the *basic* variable to be integer-valued-in-every-model
// and currently fractional; it places no integrality requirement on
// non-basic bound values, which matters here because
// PreparedLraTheory's scale-canonicalization can leave a literal
// fractional bound on a genuinely Int-sorted variable (e.g. `(<= (* 3 x)
// 5)` stores a bound of 5/3, not 1).
//
// Mutates `tableau` by adding new rows (cut slacks) and asserting bounds
// on them -- unlike gcd_inconsistent_row, this is not read-only. Callers
// that need the pre-cut state must copy first, the same discipline
// branch_and_bound already requires of its own callers. Only ever adds
// rows/bounds; never removes anything, and relies on nothing needing to
// undo a cut later -- see the module comment in lia_gomory_cuts.cpp for
// why that's sound given how this is actually called (once per outer SAT
// round, on a tableau copy discarded at the end of that round).
//
// Attempts up to `cut_limit` rounds of: find one cut-eligible fractional
// basic row (see lia_gomory_cuts.cpp for the exact eligibility test),
// derive and add its Gomory cut, re-check(). Stops early the moment
// check() reports a conflict (the cuts added so far already prove the
// tableau infeasible -- returns true). Returns false if `cut_limit` is
// exhausted, or once no cut-eligible row remains, without having proven
// infeasibility; the tableau may still have been tightened by whatever
// cuts were added along the way even in that case. Plain branch-and-bound
// (theory::arithmetic::branch_and_bound) is expected to run afterward
// regardless of this function's return value when it returns false --
// this is a pre-check that can only make branch-and-bound's job easier
// or unnecessary, never harder.
bool apply_gomory_cuts(LraTableau& tableau, const std::vector<LraVarId>& integer_vars,
                       std::size_t cut_limit);

}  // namespace primo::theory::arithmetic
