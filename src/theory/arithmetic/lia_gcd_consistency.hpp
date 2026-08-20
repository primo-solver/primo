#pragma once

#include <optional>

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
// constant, leaving an equation over the remaining free variables. Since
// every variable is Int-sorted in QF_LIA, that equation is achievable
// iff gcd(the free variables' coefficients) divides the residual; if
// not, no integer assignment can satisfy this row -- a sound,
// unconditional conflict, independent of any other bound in the problem.
//
// Scans every tableau variable, not just declared Int problem variables:
// a compound equality like `2x - 2y = 1` lives on a slack variable's row
// (PreparedLraTheory's scale-canonicalization), not a declared one.
// Returns the id of one GCD-inconsistent row's basic variable, or
// nullopt if every row is consistent. Cheap enough to run once per outer
// solve round, before branch-and-bound: an inconsistent row means
// branch-and-bound would eventually reach the same Unsat anyway, possibly
// after exhausting the whole node budget first.
std::optional<LraVarId> gcd_inconsistent_row(const LraTableau& tableau);

}  // namespace primo::theory::arithmetic
