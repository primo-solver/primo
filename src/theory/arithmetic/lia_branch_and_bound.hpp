#pragma once

#include <cstddef>
#include <vector>

#include "theory/arithmetic/lra_tableau.hpp"

namespace primo::theory::arithmetic {

// Plain branch-and-bound for QF_LIA, layered entirely on top of an
// already-built LraTableau -- no tableau/simplex change of any kind (see
// docs/qf-lia-branch-and-bound.md). Given a tableau that is already known
// LRA-sat (a prior tableau.check() returned nullopt), searches for an
// assignment where every variable in `integer_vars` also has an integer
// value, using only push()/pop()/assert_upper()/assert_lower()/check() --
// the same incremental API check_sat.cpp's QF_LRA loop already uses.
//
// On a fractional integer variable with current value v, splits on
// `var <= floor(v)` vs `var >= floor(v) + 1` (Bjørner & Nachmanson,
// "Arithmetic Solving in Z3", CAV 2024, Sect. 4.4), asserted as an
// ordinary tableau bound with a synthetic sat::Lit{0} reason -- the
// convention this codebase already uses elsewhere (e.g. check_sat.cpp's
// augmented_lra_atoms) for a tableau bound with no real SAT literal behind
// it. These synthetic-reason bounds and any conflict certificate check()
// derives from them never leave this function: on failure the caller gets
// only a Status, not a clause, and is expected to build its own conflict
// from the SAT-level atoms that produced the tableau in the first place
// (see solve_qf_lia_impl in check_sat.cpp).
//
// Explicit-stack DFS, not recursion, per this project's standing
// convention for tree search.
struct LiaBranchAndBoundResult {
  enum class Status {
    Sat,           // every integer_vars entry is now integer-valued; see below
    Unsat,         // every branch was infeasible
    LimitReached,  // node_limit was hit before Sat or Unsat was decided
  };
  Status status = Status::Unsat;
};

// **Contract on `tableau`'s final state**: on Sat, `tableau` is left
// exactly as found at the winning leaf -- the caller reads the model off
// it directly, the same way solve_qf_lra_impl already reads
// online_propagator.tableau()/model_tableau after a plain LRA check. On
// Unsat or LimitReached, every push() this function made has been undone
// via pop(), which -- per LraTableau's own documented contract -- restores
// every bound this function asserted but, like any other pop(), does NOT
// restore the assignment or basis. The tableau is left with its entry
// bounds and a valid (if numerically different) model of them, never with
// a leftover speculative branch bound.
//
// Plain branch-and-bound has no completeness argument on its own (Bjørner
// & Nachmanson, Sect. 4: it is one layer of a waterfall that also includes
// cuts) -- an unbounded integer-infeasible polytope can branch forever
// without node_limit. LimitReached is that acknowledged incompleteness,
// not a bug: see docs/qf-lia-branch-and-bound.md.
LiaBranchAndBoundResult branch_and_bound(LraTableau& tableau,
                                         const std::vector<LraVarId>& integer_vars,
                                         std::size_t node_limit);

}  // namespace primo::theory::arithmetic
