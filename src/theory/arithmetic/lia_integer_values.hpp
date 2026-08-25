#pragma once

#include "theory/arithmetic/lra_tableau.hpp"
#include "util/rational.hpp"

namespace primo::theory::arithmetic {

// Delta-aware integrality helpers shared by QF_LIA's stages. A tableau value
// is a DeltaRational, so "is this an integer" and "what is its floor" both
// have to account for the infinitesimal component -- and getting either wrong
// has already cost this project real bugs (see delta_floor's own note below).

// Whether `value` is exactly an integer. A nonzero infinitesimal disqualifies
// it however small: `n + eps` is strictly greater than `n`, so an Int variable
// sitting there has not been assigned an integer value at all.
inline bool is_integer_valued(const DeltaRational& value) {
  return value.infinitesimal.is_zero() && value.rational.is_integer();
}

// `value`'s floor, delta-aware: `value.rational.floor()` is correct on its own
// except in exactly one case -- `value.rational` is already an integer *and*
// `value.infinitesimal` is negative, i.e. the true value is `n - eps` for
// integer `n`, infinitesimally below `n` rather than at it. That value belongs
// to `[n-1, n)`, not `[n, n+1)`, so its floor is `n - 1`, not `n`.
//
// A declared Int variable can land here purely by being basic in a row whose
// other terms carry a delta (e.g. a slack at a strict, delta-trick bound --
// see bound_assertion() in check_sat.cpp), with no delta-bearing bound of its
// own. Getting this wrong previously meant branch-and-bound asserting
// `var <= n` on a variable already sitting at `n - eps` -- an already-true,
// no-op bound -- so it re-selected the same variable node after node until
// node_limit, never resolving a case it should close quickly.
inline util::Rational delta_floor(const DeltaRational& value) {
  util::Rational floor_value = value.rational.floor();
  if (value.infinitesimal.sign() < 0 && value.rational.is_integer()) {
    floor_value = floor_value - util::Rational(1);
  }
  return floor_value;
}

// `value`'s ceiling, delta-aware, by the mirror argument: `n + eps` for
// integer `n` sits in `(n, n+1]`, so its ceiling is `n + 1`, not `n`.
inline util::Rational delta_ceil(const DeltaRational& value) {
  util::Rational ceil_value = value.rational.ceil();
  if (value.infinitesimal.sign() > 0 && value.rational.is_integer()) {
    ceil_value = ceil_value + util::Rational(1);
  }
  return ceil_value;
}

}  // namespace primo::theory::arithmetic
