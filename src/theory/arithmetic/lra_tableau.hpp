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
// Assert-Bound, Check/Pivot, and Farkas-certificate-style conflict
// explanation. Bland's-rule variable selection is the paper's own choice and
// is what the termination argument rests on; three faster rules have since
// been added alongside it (see PivotingRule), each falling back to Bland
// selection past a per-call pivot threshold so termination still holds.
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
  //
  // Sparse (heuristics-note item H3) prices what a pivot *costs* rather than
  // what it achieves: it keeps Dantzig's most-violated leaving choice and
  // picks, among the eligible entering candidates, the one occurring in the
  // fewest rows. pivot() rewrites every row in occurs_in_[entering], merging
  // in a row of length |row(leaving)|, so a pivot's cost and the fill-in it
  // creates are the Markowitz product |occurs_in(entering)| * |row(leaving)|
  // -- and none of Bland, Dantzig or Soi prices either factor. Crucially,
  // once the leaving variable is fixed, *every* eligible entering candidate
  // repairs the same violation by the same amount, so this choice is free
  // with respect to progress: Bland and Dantzig are already picking
  // arbitrarily among equivalent options, and Sparse just picks the cheapest
  // of them. Same anti-cycling story as Dantzig (fallback to Bland selection
  // past a per-call pivot threshold). Source: Monniaux, "On using
  // floating-point computations to help an exact linear arithmetic decision
  // procedure" (CAV 2009), p.8, which sorts by "number of occurrences in
  // equations" for the same reason -- only that sparsity criterion is taken
  // from the paper, not its floating-point preprocessing contribution.
  //
  // Note this rule is the field's standard, not a technique transferred from
  // one paper: King's thesis ("Effective Algorithms for the Satisfiability of
  // Quantifier-Free Formulas Over Linear Real and Integer Arithmetic", NYU
  // 2014) Section 2.2.12 records minimum-column-length entering selection,
  // with a switch to Bland's rule past a finite cap, as what MathSAT,
  // OpenSMT, Yices, Yices 2, z3 and CVC4 all do. The leaving-side knob
  // (set_sparse_leaving) is the part with no counterpart described there --
  // King records Griggio's most-violated rule, which is our Dantzig choice.
  //
  // NOTE: Sparse (with set_sparse_leaving) is the *solver's* default since
  // 2026-07-29; this class still constructs Bland by default, because Bland
  // is the conservative choice for a directly-constructed tableau and the
  // white-box tests rely on it. CheckSatOptions::lra_pivoting_rule is what
  // sets the solver's.
  enum class PivotingRule { Bland, Dantzig, Soi, Sparse };

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

  // PivotingRule::Sparse only: prefer a violated variable with a short row
  // when choosing which one to repair, rather than the most-violated one.
  // This is the *other* Markowitz factor (Monniaux p.8's first sorting
  // criterion), and unlike the entering-side choice it is NOT
  // progress-neutral -- it changes which infeasibility gets repaired -- so it
  // is a separate opt-in knob, measured on its own rather than confounded
  // with the entering-side rule. Off by default; no effect under any other
  // pivoting rule. Intended to be set before solving starts (like add_row);
  // it is not trailed, so flipping it mid-search would change selection
  // without any record of having done so.
  void set_sparse_leaving(bool enabled) { sparse_leaving_ = enabled; }

  // -- Fixed-variable elimination (heuristics-note item H7) --
  //
  // A basic variable whose lower and upper bounds coincide is *fixed*: its
  // row is an equality constraint that must hold exactly, so
  // update_violation_status() marks it violated the moment any non-basic
  // term in it moves, and check() has to spend a repair pivot on it again
  // and again. Real QF_LRA benchmarks are full of these -- an equality
  // `t1 = t2` lowers to two inequalities that share one slack (item H1b),
  // which then carries an upper and a lower bound at the same value -- and
  // on the profiled competition instances they are the dominant source of
  // pivoting.
  //
  // When enabled, assert_upper/assert_lower pivot such a variable out of the
  // basis as soon as it becomes fixed. Once non-basic it is completely inert:
  // can_enter() rejects a fixed variable on every branch (lower == upper ==
  // assignment makes each of its `assignment < upper` / `assignment > lower`
  // tests false), so it is never chosen again and its column never moves.
  // This is the standard "eliminate fixed variables" step; see
  // docs/qf-lra-heuristics.md item H7 for the measurements motivating it.
  //
  // The pivot's entering variable is preferred unbounded where possible,
  // since an unbounded basic variable's row can never be violated and so
  // never demands another repair. The pivot is trailed by pivot() and undone
  // by pop() like any other.
  //
  // Off by default, everywhere, including in the solver. The 595-file
  // scrambled competition set is where this was decided: 374 solved against
  // 373 with it off, PAR2 29237 against 29204, and a *median 0.94x* speedup
  // over the files both configurations solve in more than a second -- +19/-18
  // file churn that nets to nothing. The one real effect is confined to the
  // largest instances, where it doubles the >5MB band (7 -> 14 of 50, matching
  // z3), and it is paid for in the 50-500KB band (144 -> 140). Enable with
  // --lra-fixed-elimination when the input is dominated by equalities; see
  // docs/qf-lra-heuristics.md item H7 for the full falsifier outcome.
  //
  // An earlier version also *froze* a qualifying row out of the working set
  // entirely. That half was strictly negative on the same run (it cost four
  // files in the 500KB-5MB band and gained one elsewhere) while carrying all
  // of this item's wrong-answer risk, so it was removed rather than tuned.
  void set_fixed_elimination(bool enabled) { fixed_elimination_ = enabled; }

  // Item H7 observability, same convention as row_update_count() (cumulative
  // over this tableau's lifetime, snapshot for a per-round delta):
  //
  // fixed_variable_count: basic variables observed to become fixed.
  // elimination_pivot_count: pivots spent eliminating them. The shortfall
  //   against fixed_variable_count is the eliminations that did not happen at
  //   all -- an empty row, or every candidate already fixed itself.
  //
  // These all count into pivot_count() as well, since they are real pivots.
  // The published :lra.pivots statistic is a different matter and is NOT
  // comparable across an H7 ablation on its own: check_sat.cpp samples it as
  // a delta around check(), while elimination pivots happen inside
  // assert_upper/assert_lower, so they fall outside that window entirely.
  // :lra.pivots dropping with H7 on therefore does not by itself mean less
  // pivoting -- add :lra.elimination-pivots, or compare
  // :lra.pivot-row-updates / :lra.pivot-term-ops, which count every pivot's
  // actual work regardless of where it was triggered.
  std::size_t fixed_variable_count() const { return fixed_variable_count_; }
  std::size_t elimination_pivot_count() const { return elimination_pivot_count_; }

  // Cost accounting for the Check/Pivot loop, in the two quantities item H3
  // actually minimizes -- pivot *count* alone cannot distinguish "fewer
  // pivots" from "cheaper pivots", and the whole risk of a sparsity rule is
  // that it trades one for the other. Cumulative across this tableau's
  // lifetime; callers wanting a per-round delta snapshot before and after,
  // the same convention pivot_count() uses.
  //
  // row_update_count: rows rewritten, i.e. sum of |occurs_in(entering)|.
  // row_term_op_count: terms merged while rewriting them.
  std::size_t row_update_count() const { return row_update_count_; }
  std::size_t row_term_op_count() const { return row_term_op_count_; }

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

  // check()'s entering-variable choice, split out so the three rules stay
  // readable side by side. can_enter() is the eligibility test they share --
  // every candidate passing it repairs the violation by the same amount.
  bool can_enter(const util::Rational& coeff, const VarState& state, bool too_low) const;
  LraVarId select_entering_variable(LraVarId violated, bool too_low, bool use_bland);
  LraVarId select_sparse_entering_variable(const Row& row, bool too_low);

  // Item H7 (see set_fixed_elimination): if `var` has just become a fixed
  // basic variable, pivot it out and, when the entering variable's resulting
  // row constrains nothing, deactivate that row. Returns whether an
  // elimination pivot was performed. No-op unless fixed_elimination_ is set.
  bool try_eliminate_fixed_basic(LraVarId var);
  // Entering choice for an elimination pivot. Unlike check()'s, every term of
  // the row is eligible (the point is to leave the basis, not to repair a
  // violation in a particular direction), so this ranks rather than filters:
  // an unbounded candidate first, then the Markowitz occurrence count, then a
  // +/-1 coefficient, then index. Already-fixed candidates are skipped
  // outright -- pivoting one of those *into* the basis is the state this
  // whole mechanism exists to get out of.
  LraVarId select_elimination_entering_variable(const Row& row) const;
  // Whether `var`'s lower and upper bounds coincide, i.e. it is a constant
  // for the remainder of the current search level.
  bool is_fixed(LraVarId var) const;
  // Whether `var` currently has no bound on either side. An unbounded basic
  // variable's row can never be violated, so it never demands a repair pivot
  // -- which is why the elimination pivot prefers such a candidate.
  bool is_unbounded(LraVarId var) const;

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

  // PivotingRule::Sparse: how many *eligible* entering candidates to score
  // before settling for the best seen so far. Rows reach several hundred
  // terms on real instances (691 on latendresse/ecoliFBAyicesTest-500), and
  // scoring all of them would spend on selection what the rule is trying to
  // save on the pivot. Not precisely derived; small enough to bound the scan,
  // large enough that the sparsest candidate is usually among those seen.
  //
  // This caps only how many eligible candidates are *scored*. It must never
  // cut the scan short of establishing that at least one exists: "no eligible
  // entering candidate" is exactly what check() reports as a conflict, so
  // giving up early without having found one would manufacture an unsound
  // conflict rather than merely a worse pivot. See select_entering_variable.
  static constexpr std::size_t kSparsePricingCandidateLimit = 16;

  PivotingRule rule_;
  bool sparse_leaving_ = false;
  bool fixed_elimination_ = false;
  // Rotating start offset for Sparse's bounded candidate scan. pivot()
  // rebuilds rows in sorted-by-variable-id order, so always starting at the
  // front would bias the bounded scan toward low-numbered variables; rotating
  // spreads it. Search state, not tableau state -- deliberately not trailed,
  // since it only affects which equally-valid pivot is chosen.
  std::size_t sparse_pricing_cursor_ = 0;
  std::size_t bland_fallback_count_ = 0;
  std::size_t pivot_count_ = 0;
  std::size_t row_update_count_ = 0;
  std::size_t row_term_op_count_ = 0;
  std::size_t soi_multi_row_conflict_count_ = 0;
  std::size_t soi_quickxplain_call_count_ = 0;
  std::size_t fixed_variable_count_ = 0;
  std::size_t elimination_pivot_count_ = 0;
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
