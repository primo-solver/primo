#pragma once

#include <cstddef>
#include <optional>
#include <set>
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

  static DeltaRational from_rational(util::Rational value) {
    return DeltaRational{std::move(value), util::Rational(0)};
  }

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
  // suffix is still covered by the paper's termination argument. Soi
  // (competitiveness-roadmap item 4, King/Barrett/Dutertre FMCAD 2013,
  // "Simplex with Sum of Infeasibilities for SMT") replaces the
  // single-row objective with a global one -- minimize the sum of
  // infeasibilities across *every* out-of-bound basic variable at once --
  // giving pivot selection a global progress criterion Bland/Dantzig
  // both lack. See the SOI-specific members/methods below.
  enum class PivotingRule { Bland, Dantzig, Soi };

  explicit LraTableau(PivotingRule rule = PivotingRule::Bland) : rule_(rule) {
    if (rule_ == PivotingRule::Soi) {
      // The objective row (Section V): a real extra variable/row so
      // pivot()'s existing, already-correct substitution logic keeps it
      // consistent across basis changes for free -- only the d_i-flip-
      // driven additive update (set_soi_direction()) is genuinely new.
      // Empty initially: no variable is in error before any bounds are
      // asserted, so every d_i starts at 0.
      f_var_ = add_variable();
      add_row(*f_var_, {});
    }
  }

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

  // Cumulative count of pivot() calls across this tableau's lifetime --
  // diagnostic only (measuring whether check()'s current eager-per-literal
  // invocation from OnlineLraPropagator::notify_assignment_lit does
  // redundant repair work across closely-spaced assignments, a documented
  // opportunity noted at that call site). Callers that want a per-round
  // delta should snapshot this before/after, same convention as
  // bland_fallback_count().
  std::size_t pivot_count() const { return pivot_count_; }

  // PivotingRule::Soi observability: how many times check() had to build a
  // multi-row conflict (Section V-B, Flex(f) empty with 2+ rows still
  // violated), and how many of those invoked QuickXplain-style
  // minimization (minimize_soi_conflict()). Always 0 under Bland/Dantzig.
  std::size_t soi_multi_row_conflict_count() const { return soi_multi_row_conflict_count_; }
  std::size_t soi_quickxplain_call_count() const { return soi_quickxplain_call_count_; }

  // Total number of tableau variables (problem variables plus slacks, plus
  // f_var_ under PivotingRule::Soi). Only grows during initial construction
  // -- see the class comment -- so this is a structural size, not search
  // state. Used to publish the :lra.tableau-vars / :lra.rows /
  // :lra.row-nonzeros statistics, which are what make the tableau's shape
  // (and so the effect of anything that changes how many rows a formula
  // needs) measurable instead of a matter of inference.
  std::size_t variable_count() const { return vars_.size(); }

  // Whether `var` is currently a basic (row-defining) variable -- pivots
  // can change this over time, so this reflects the current pivot state,
  // not a static structural property.
  bool is_basic(LraVarId var) const { return vars_.at(var).is_basic; }

  // `var`'s current row (basic_var = sum(coefficient * var)). `var` must
  // currently be basic (see is_basic()); throws std::out_of_range
  // otherwise, matching this class's existing .at()-style conventions.
  // Note: rows_ stores a slot for every variable (not just currently-basic
  // ones -- see rows_'s own comment), so this only throws for a genuinely
  // invalid `var`, not for a merely-nonbasic one; callers must still gate
  // on is_basic() themselves, since a nonbasic slot's content is stale.
  const Row& row(LraVarId var) const { return rows_.at(var); }

  // The basic variable ids of every row whose terms currently mention
  // `var` -- the reverse of row() -- used by row-based/interval bound
  // propagation to find which rows could have a newly-tightened implied
  // bound after `var`'s own bound changes. Empty (not an error) if `var`
  // occurs in no row. Reflects the current pivot state, kept correct
  // across add_row/pivot/pop (see occurs_in_'s own maintenance).
  const std::vector<LraVarId>& occurs_in(LraVarId var) const { return occurs_in_.at(var); }

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
  // PivotingRule::Soi only: records one set_soi_direction() call's effect
  // on f_var_'s row and on `var`'s tracked direction, atomically, so
  // pop() can't desync the two (see set_soi_direction()'s comment).
  struct FRowTrailEntry {
    Row previous_f_row;
    LraVarId var;
    int previous_direction;
  };
  using TrailEntry =
      std::variant<BoundTrailEntry, AssignmentTrailEntry, PivotTrailEntry, FRowTrailEntry>;

  void update(LraVarId nonbasic_var, const DeltaRational& new_value);
  void pivot(LraVarId basic_var, LraVarId entering_var);
  std::vector<sat::Lit> build_conflict(LraVarId violated, bool too_low) const;
  void undo(const TrailEntry& entry);
  void rebuild_occurs_in_index();

  // Keeps violated_vars_ (see its own comment) in sync going *forward*:
  // checks `var`'s current is_basic/assignment/bounds and inserts/erases
  // it from the set accordingly. Called from the two places a basic
  // variable's violation status can change during ordinary (non-undo)
  // operation: assert_upper/assert_lower's is_basic branch, and update()'s
  // dependent-basic-variable assignment-change loop. A non-basic variable
  // is never a member (there is nothing to remove if `var` just became
  // non-basic via pivot() -- see pivot()'s own call site for why that's
  // still handled correctly without a dedicated hook there).
  void update_violation_status(LraVarId var);

  // Full O(vars_.size()) rebuild of violated_vars_ from scratch, mirroring
  // rebuild_occurs_in_index()'s existing precedent: pop() can undo bound/
  // assignment/pivot/f-row trail entries in combinations too varied to
  // patch incrementally without real correctness risk, but pop() itself
  // is rare (once per backtrack, not once per pivot), so a full rescan
  // here is cheap relative to what it replaces.
  void rebuild_violated_vars();

  // -- PivotingRule::Soi (Section V) --

  // eq (3)'s directional multiplier: -1 if `var` (assumed currently
  // basic) is below its lower bound, +1 if above its upper bound, 0 if
  // within bounds (or unbounded on the relevant side).
  int soi_direction(LraVarId var) const;

  // The incremental core of Section V's f-row maintenance ("efficiently...
  // by instrumenting UPDATE"): if `new_direction` differs from `var`'s
  // last-recorded direction, merges `(new_direction - old) * var_row`
  // into f_var_'s row (rebuilding that Row + f_var_'s occurs_in_ entries)
  // and records an FRowTrailEntry. `var_row` is passed explicitly (rather
  // than read via rows_.at(var) internally) because the leaving-variable
  // reset call in pivot() must run *before* that row is erased from
  // rows_ -- see pivot()'s call site.
  void set_soi_direction(LraVarId var, int new_direction, const Row& var_row);

  // One SOISELECT call (Fig 5, Section V-A): picks the entering variable,
  // leaving row, and step size that most reduces the sum-of-infeasibilities
  // objective, via the breakpoint-sorted DeltaVio(j,delta) walk. Returns
  // nullopt if no candidate makes progress (Flex(f) is empty) -- the
  // caller must then check whether E is also empty (satisfiable) or not
  // (stuck -- build_soi_conflict()).
  struct SoiSelection {
    LraVarId entering;
    LraVarId leaving;
    DeltaRational delta;
  };
  std::optional<SoiSelection> soi_select() const;

  // f_S = sum of d_i*row_i over only `subset` -- shared by
  // build_soi_conflict and soi_subset_still_conflicts.
  std::unordered_map<LraVarId, util::Rational> restricted_soi_row(
      const std::vector<LraVarId>& subset) const;

  // Section V-B's multi-row conflict, restricted to `error_vars` (the
  // full violated set, or a minimize_soi_conflict()-shrunk subset of it):
  // f_S's own row (same sign-branching pattern as build_conflict) plus
  // every var in `error_vars`'s own bound reason.
  std::vector<sat::Lit> build_soi_conflict(const std::vector<LraVarId>& error_vars) const;

  // Standard QuickXplain (Junker 2001, cited by the paper as [21]) over
  // `error_vars`, using an is-still-a-conflict oracle that computes a
  // *restricted* f_S (sum of d_i*row_i over only a candidate subset) and
  // checks whether any nonbasic variable could still locally reduce it.
  // Returns a subset of `error_vars` sufficient for build_soi_conflict to
  // still produce a sound conflict -- not necessarily globally minimal,
  // but usually is in practice (paper: "most of the time... |S|=2").
  std::vector<LraVarId> minimize_soi_conflict(const std::vector<LraVarId>& error_vars) const;
  bool soi_subset_still_conflicts(const std::vector<LraVarId>& subset) const;
  // The recursive QuickXplain step: minimal subset of `candidates` that,
  // combined with `background`, still conflicts. Standard algorithm
  // (Junker 2001) -- not itself paper-specific, just the well-known
  // divide-and-conquer minimization technique the paper cites by name.
  std::vector<LraVarId> quickxplain(const std::vector<LraVarId>& background,
                                    const std::vector<LraVarId>& candidates) const;

  PivotingRule rule_;
  std::size_t bland_fallback_count_ = 0;
  std::size_t pivot_count_ = 0;
  std::size_t soi_multi_row_conflict_count_ = 0;
  std::size_t soi_quickxplain_call_count_ = 0;
  std::optional<LraVarId> f_var_;
  // soi_directions_/rows_/occurs_in_ are all dense, indexed directly by
  // LraVarId (a plain index into vars_, not an opaque key) -- a further
  // profiling follow-up beyond combine_scratch_'s own fix (see its
  // comment): rows_/occurs_in_ were still std::unordered_map, so every
  // pivot's rows_.erase+emplace and occurs_in_ insert/erase was itself a
  // per-call heap-allocated map-node churn, independent of and additional
  // to the combine step's own cost -- re-profiling after the
  // combine_scratch_ fix confirmed this (std::__detail::_Map_base still
  // hot). Since LraVarId is dense and only grows during initial tableau
  // construction (add_variable()/add_row(), always called before the
  // first push()/check() -- see the class comment), sizing these vectors
  // in lockstep with vars_ (in add_variable()) makes every index always
  // valid: a slot for a currently-nonbasic variable simply holds stale,
  // unread content (rebuild_occurs_in_index() gates by is_basic rather
  // than by presence, exactly to make this safe -- see its own comment).
  std::vector<int> soi_directions_;
  std::vector<VarState> vars_;
  std::vector<Row> rows_;
  std::vector<std::vector<LraVarId>> occurs_in_;
  // Currently-violated basic variable ids (assignment outside its own
  // bound), kept incrementally in sync -- see update_violation_status()'s
  // comment. A sorted set gives Bland's smallest-index selection for
  // free and lets Dantzig/SOI's own scans iterate just this (typically
  // small) set instead of every variable in the tableau.
  // `soi_direction(var) != 0` is exactly equivalent to `var ∈
  // violated_vars_` by construction (both mean "basic and currently out
  // of bounds"), so this one set also serves SOI's error-var collection
  // in check() -- no separate SOI-specific bookkeeping needed.
  std::set<LraVarId> violated_vars_;
  std::vector<TrailEntry> trail_;
  // Reused across pivot()'s per-affected-row combine step and
  // set_soi_direction()'s f-row combine step, both of which need to merge
  // two sparse coefficient rows. Profiling (perf, on
  // tmp/QF_LRA/sc/sc-21.induction3.cvc.smt2 and
  // tmp/QF_LRA/uart/uart-18.induction.cvc.smt2, both of which time out at
  // 10s while z3/cvc5 solve in under 1s) showed malloc/free/GMP-allocation
  // machinery consuming 50-60% of runtime. A first attempt reused an
  // unordered_map member instead of a fresh local -- that did NOT help:
  // unordered_map is node-based, so every element is its own heap
  // allocation regardless of whether the container itself is reused
  // (clear() frees each node). A flat vector, sorted then merged, does the
  // same combine with only this member's own backing array (which clear()
  // does NOT deallocate) -- no per-element heap allocation at all beyond
  // each Rational's own unavoidable GMP storage. Safe to share one
  // instance: neither user ever holds it populated across a call into the
  // other (pivot()'s own set_soi_direction() call happens before pivot()'s
  // own use of this member, and completes fully first), and LraTableau is
  // not reentrant.
  std::vector<std::pair<LraVarId, util::Rational>> combine_scratch_;
};

}  // namespace primo::theory::arithmetic
