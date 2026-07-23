#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sat/lit.hpp"
#include "util/rational.hpp"

namespace primo::theory::arithmetic {

using LraVarId = std::size_t;

// A value in the "rational + infinitesimal" domain: q + k*delta for a
// symbolic infinitesimal delta > 0, used to represent strict-inequality
// bounds/assignments exactly without picking a concrete delta (Dutertre &
// de Moura, "A Fast Linear-Arithmetic Solver for DPLL(T)", Section 3.1).
// Ordered lexicographically on (rational, infinitesimal).
struct DeltaRational {
  util::Rational rational{0};
  util::Rational infinitesimal{0};

  static DeltaRational from_rational(util::Rational value) { return DeltaRational{std::move(value), util::Rational(0)}; }

  DeltaRational operator+(const DeltaRational& other) const {
    return DeltaRational{rational + other.rational, infinitesimal + other.infinitesimal};
  }
  DeltaRational operator-(const DeltaRational& other) const {
    return DeltaRational{rational - other.rational, infinitesimal - other.infinitesimal};
  }
  DeltaRational operator-() const { return DeltaRational{-rational, -infinitesimal}; }
  DeltaRational scaled(const util::Rational& factor) const {
    return DeltaRational{rational * factor, infinitesimal * factor};
  }

  bool operator==(const DeltaRational& other) const {
    return rational == other.rational && infinitesimal == other.infinitesimal;
  }
  bool operator!=(const DeltaRational& other) const { return !(*this == other); }
  bool operator<(const DeltaRational& other) const {
    if (rational != other.rational) {
      return rational < other.rational;
    }
    return infinitesimal < other.infinitesimal;
  }
  bool operator<=(const DeltaRational& other) const { return *this < other || *this == other; }
  bool operator>(const DeltaRational& other) const { return other < *this; }
  bool operator>=(const DeltaRational& other) const { return other <= *this; }
};

// A bounded-variable Simplex tableau for QF_LRA, implementing Section 3 of
// Dutertre & de Moura ("A Fast Linear-Arithmetic Solver for DPLL(T)"):
// Assert-Bound, Check/Pivot with Bland's-rule variable selection (for
// termination, not performance -- see docs/papers/lra-related-work.md for
// faster pivoting rules a later milestone could adopt), and Farkas-
// certificate-style conflict explanation. Batch/non-incremental in this
// milestone: no push/pop, callers rebuild a fresh tableau per SAT round.
//
// Operates on abstract dense variable ids (LraVarId), not core::TermId --
// the caller (check_sat.cpp's solve_qf_lra) is responsible for mapping
// problem/slack variables to ids, mirroring how FlatCongruenceClosure
// operates on FlatTermId rather than core::TermId directly.
class LraTableau {
 public:
  // Allocates a fresh non-basic variable with assignment 0 and no bounds.
  LraVarId add_variable();

  // Defines a basic variable's row: basic_var = sum(coefficient * var).
  // basic_var must not already have a row. Coefficients of zero are
  // dropped; an empty `terms` is allowed (basic_var's row is the constant
  // 0 -- reachable when two syntactically different atoms normalize to the
  // same polynomial, see lra_atom.cpp).
  void add_row(LraVarId basic_var, const std::vector<std::pair<LraVarId, util::Rational>>& terms);

  // Section 3.1's Assert-Bound. Returns a conflict (the reasons of the two
  // directly-contradicting bounds) if the new bound is immediately
  // inconsistent with the existing opposite bound on the same variable;
  // otherwise records the bound (updating the current assignment, and
  // propagating to dependent basic variables, if var is non-basic) and
  // returns nullopt. A bound weaker than the existing one is a silent
  // no-op.
  std::optional<std::vector<sat::Lit>> assert_upper(LraVarId var, DeltaRational value,
                                                     sat::Lit reason);
  std::optional<std::vector<sat::Lit>> assert_lower(LraVarId var, DeltaRational value,
                                                     sat::Lit reason);

  // Section 3.1's Check. Repeatedly pivots to repair out-of-bounds basic
  // variables until either every basic variable is within its bounds
  // (returns nullopt -- satisfiable, see assignment()) or some basic
  // variable is out of bounds with no repairing pivot available (returns
  // its Farkas-style conflict certificate, Section 3.2).
  std::optional<std::vector<sat::Lit>> check();

  const DeltaRational& assignment(LraVarId var) const { return vars_.at(var).assignment; }

 private:
  struct Bound {
    DeltaRational value;
    sat::Lit reason;
  };

  struct VarState {
    bool is_basic = false;
    DeltaRational assignment;
    std::optional<Bound> lower;
    std::optional<Bound> upper;
  };

  using Row = std::vector<std::pair<LraVarId, util::Rational>>;

  void update(LraVarId nonbasic_var, const DeltaRational& new_value);
  void pivot(LraVarId basic_var, LraVarId entering_var);
  std::vector<sat::Lit> build_conflict(LraVarId violated, bool too_low) const;

  std::vector<VarState> vars_;
  std::unordered_map<LraVarId, Row> rows_;
  std::unordered_map<LraVarId, std::vector<LraVarId>> occurs_in_;
};

}  // namespace primo::theory::arithmetic
