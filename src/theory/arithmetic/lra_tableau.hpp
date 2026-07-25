#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>
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
// certificate-style conflict explanation.
//
// Incremental via push()/pop(checkpoint), genuinely trail-based (mirroring
// FlatCongruenceClosure's discipline, not full state snapshotting): every
// bound change, assignment change, and pivot's row rewrite pushes a
// TrailEntry recording enough to undo it, and pop() replays entries
// backwards. Per the paper's Section 4 design, pivots (which basic/
// non-basic partition and row structure are current) are NOT semantically
// tied to search depth -- they're just an internal representation choice
// -- but they still have to be undone on pop here, because leaving a
// pivot's row rewrite in place while reverting the bounds/assignments it
// was performed under would violate the invariant that every basic
// variable's row equation holds for the current assignment.
//
// Rows must all be added via add_row() before the first push() in the
// caller's intended usage (build the full tableau once, then push/assert/
// pop as the SAT search proceeds) -- add_row() itself is not trailed.
//
// Operates on abstract dense variable ids (LraVarId), not core::TermId --
// the caller (check_sat.cpp's solve_qf_lra) is responsible for mapping
// problem/slack variables to ids, mirroring how FlatCongruenceClosure
// operates on FlatTermId rather than core::TermId directly.
class LraTableau {
 public:
  // Selects how check()'s Check/Pivot loop picks which out-of-bound basic
  // variable to repair and which non-basic variable enters the basis.
  // Bland is the paper's own choice (smallest-index for both), the only
  // one with a direct termination proof. Dantzig (largest violation /
  // largest |coefficient|) is cheaper per pivot and faster in practice
  // (docs/papers/lra-related-work.md's survey) but has no anti-cycling
  // guarantee on its own -- check() falls back to Bland's-rule selection
  // after enough pivots within one call to still terminate; see check()'s
  // definition for why a finite Dantzig prefix followed by a Bland's-rule
  // suffix is still covered by the paper's termination argument.
  enum class PivotingRule { Bland, Dantzig };

  explicit LraTableau(PivotingRule rule = PivotingRule::Bland) : rule_(rule) {}

  // A basic variable's row: basic_var = sum(coefficient * var), one
  // (var, coefficient) pair per term. Not sorted by var id.
  using Row = std::vector<std::pair<LraVarId, util::Rational>>;

  // Allocates a fresh non-basic variable with assignment 0 and no bounds.
  LraVarId add_variable();

  // Defines a basic variable's row: basic_var = sum(coefficient * var).
  // basic_var must not already have a row. Coefficients of zero are
  // dropped; an empty `terms` is allowed (basic_var's row is the constant
  // 0 -- reachable when two syntactically different atoms normalize to the
  // same polynomial, see lra_atom.cpp). Not trailed -- see the class
  // comment; must be called before the first push().
  void add_row(LraVarId basic_var, const std::vector<std::pair<LraVarId, util::Rational>>& terms);

  // Returns a checkpoint token for the current trail position.
  std::size_t push() const { return trail_.size(); }

  // Undoes every bound/assignment/pivot change made since `checkpoint`
  // (which must have come from an earlier push()), in reverse order.
  void pop(std::size_t checkpoint);

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

  // Read-only access to a variable's current upper/lower bound (if any) and
  // the literal that established it -- used by OnlineLraPropagator's
  // same-slack "deriving satisfied atoms" propagation (Dutertre & de
  // Moura, Section 4.2) to find bounds tight enough to entail other atoms
  // on the same slack variable.
  struct BoundInfo {
    DeltaRational value;
    sat::Lit reason;
  };
  std::optional<BoundInfo> upper_bound(LraVarId var) const {
    const auto& bound = vars_.at(var).upper;
    if (!bound.has_value()) {
      return std::nullopt;
    }
    return BoundInfo{bound->value, bound->reason};
  }
  std::optional<BoundInfo> lower_bound(LraVarId var) const {
    const auto& bound = vars_.at(var).lower;
    if (!bound.has_value()) {
      return std::nullopt;
    }
    return BoundInfo{bound->value, bound->reason};
  }

  // Cumulative count of check() calls (across this tableau's lifetime,
  // including anything replayed via copy) that hit the Dantzig anti-cycling
  // fallback -- always 0 under PivotingRule::Bland. Callers that want a
  // per-round delta (e.g. for a statistics counter) should snapshot this
  // before and after a check() call themselves.
  std::size_t bland_fallback_count() const { return bland_fallback_count_; }

  // Whether `var` is currently a basic (row-defining) variable -- pivots
  // can change this over time, so this reflects the current pivot state,
  // not a static structural property.
  bool is_basic(LraVarId var) const { return vars_.at(var).is_basic; }

  // `var`'s current row (basic_var = sum(coefficient * var)). `var` must
  // currently be basic (see is_basic()); throws std::out_of_range
  // otherwise, matching this class's existing .at()-style conventions.
  const Row& row(LraVarId var) const { return rows_.at(var); }

  // The basic variable ids of every row whose terms currently mention
  // `var` -- the reverse of row() -- used by row-based/interval bound
  // propagation to find which rows could have a newly-tightened implied
  // bound after `var`'s own bound changes. Empty (not an error) if `var`
  // occurs in no row. Reflects the current pivot state, kept correct
  // across add_row/pivot/pop (see occurs_in_'s own maintenance).
  const std::vector<LraVarId>& occurs_in(LraVarId var) const {
    static const std::vector<LraVarId> kEmpty;
    const auto found = occurs_in_.find(var);
    return found == occurs_in_.end() ? kEmpty : found->second;
  }

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

  struct BoundTrailEntry {
    LraVarId var;
    bool is_upper;
    std::optional<Bound> previous;
  };
  struct AssignmentTrailEntry {
    LraVarId var;
    DeltaRational previous;
  };
  // A pivot's row rewrite touches the pivoted basic/entering pair plus
  // every other basic row that referenced entering_var -- snapshot every
  // row touched (before the rewrite) rather than trying to diff them.
  struct PivotTrailEntry {
    LraVarId basic_var;
    LraVarId entering_var;
    Row old_basic_row;
    std::vector<std::pair<LraVarId, Row>> old_other_rows;
  };
  using TrailEntry = std::variant<BoundTrailEntry, AssignmentTrailEntry, PivotTrailEntry>;

  void update(LraVarId nonbasic_var, const DeltaRational& new_value);
  void pivot(LraVarId basic_var, LraVarId entering_var);
  std::vector<sat::Lit> build_conflict(LraVarId violated, bool too_low) const;
  void undo(const TrailEntry& entry);
  void rebuild_occurs_in_index();

  PivotingRule rule_;
  std::size_t bland_fallback_count_ = 0;
  std::vector<VarState> vars_;
  std::unordered_map<LraVarId, Row> rows_;
  std::unordered_map<LraVarId, std::vector<LraVarId>> occurs_in_;
  std::vector<TrailEntry> trail_;
};

}  // namespace primo::theory::arithmetic
