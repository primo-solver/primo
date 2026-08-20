#pragma once

#include <optional>
#include <vector>

#include "theory/arithmetic/lra_tableau.hpp"

namespace primo::theory::arithmetic {

// QF_LIA GCD-consistency (stage 2, docs/qf-lia-branch-and-bound.md): a
// read-only structural check over the tableau's own row data -- no
// LraTableau mutation, unlike branch-and-bound's bound assertions.
//
// For a row `x_b = sum(a_j * x_j)`, fold every currently-*fixed*
// variable in it (x_b itself included -- which variable in a given
// linear relationship happens to be basic can change after a Simplex
// pivot without changing which underlying variables are actually fixed,
// so both sides of the row are treated symmetrically) into a residual
// constant, leaving an equation over the remaining free variables. A
// *declared* QF_LIA problem variable ranges over all of Z, so if it is
// one of `integer_vars` and not fixed, the equation is achievable iff
// gcd(the free variables' coefficients) divides the residual. A row's
// basic variable specifically needs its own check for this (unlike a
// row's other terms, which -- see PreparedLraTheory's row construction
// -- are always declared problem variables, so always in `integer_vars`):
// PreparedLraTheory's scale-canonicalization (item H1b) can make a
// non-fixed *basic* variable a slack representing the original
// polynomial divided by a non-unit scale, which is not itself
// integer-valued even though every declared variable feeding it is --
// the same hazard lia_gomory_cuts.cpp's find_eligible_row already
// documents for exactly this reason. Treating such a slack as if it too
// ranged over Z (this module's own bug prior to the fix that added
// `integer_vars` here, found via real-world QF_LIA benchmark evaluation,
// docs/qf-lia-branch-and-bound.md) fabricates conflicts a correct read
// of the row's actual value range would not support. When the basic
// variable isn't fixed and isn't in
// `integer_vars`, its row carries no GCD information at all (the row is
// trivially satisfiable by letting that variable take whatever
// non-integer value the free terms produce) and is reported consistent.
//
// Scans every tableau variable, not just declared Int problem variables:
// a compound equality like `2x - 2y = 1` lives on a slack variable's row
// (PreparedLraTheory's scale-canonicalization), not a declared one -- but
// there the slack is exactly *fixed* by the equality itself, so the
// non-fixed/non-integer-slack case above does not apply to it.
// Returns the id of one GCD-inconsistent row's basic variable, or
// nullopt if every row is consistent. Cheap enough to run once per outer
// solve round, before branch-and-bound: an inconsistent row means
// branch-and-bound would eventually reach the same Unsat anyway, possibly
// after exhausting the whole node budget first.
std::optional<LraVarId> gcd_inconsistent_row(const LraTableau& tableau,
                                             const std::vector<LraVarId>& integer_vars);

}  // namespace primo::theory::arithmetic
