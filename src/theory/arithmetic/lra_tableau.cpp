#include "theory/arithmetic/lra_tableau.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace primo::theory::arithmetic {

LraVarId LraTableau::add_variable() {
  const LraVarId id = vars_.size();
  vars_.push_back(VarState{});
  rows_.emplace_back();
  occurs_in_.emplace_back();
  soi_directions_.push_back(0);
  return id;
}

void LraTableau::add_row(LraVarId basic_var, const std::vector<std::pair<LraVarId, util::Rational>>& terms) {
  Row row;
  row.reserve(terms.size());
  DeltaRational assignment = DeltaRational::from_rational(util::Rational(0));
  for (const auto& [var, coefficient] : terms) {
    if (coefficient.is_zero()) {
      continue;
    }
    row.emplace_back(var, coefficient);
    occurs_in_[var].push_back(basic_var);
    assignment = assignment + vars_.at(var).assignment.scaled(coefficient);
  }
  vars_.at(basic_var).is_basic = true;
  vars_.at(basic_var).assignment = assignment;
  rows_[basic_var] = std::move(row);
}

std::optional<std::vector<sat::Lit>> LraTableau::assert_upper(LraVarId var, DeltaRational value,
                                                               sat::Lit reason) {
  VarState& state = vars_.at(var);
  if (state.upper.has_value() && value >= state.upper->value) {
    return std::nullopt;
  }
  if (state.lower.has_value() && value < state.lower->value) {
    return std::vector<sat::Lit>{state.lower->reason, reason};
  }
  trail_.push_back(BoundTrailEntry{var, true, state.upper});
  state.upper = Bound{value, reason};
  if (!state.is_basic) {
    if (state.assignment > value) {
      update(var, value);
    }
  } else if (rule_ == PivotingRule::Soi) {
    set_soi_direction(var, soi_direction(var), rows_.at(var));
  }
  return std::nullopt;
}

std::optional<std::vector<sat::Lit>> LraTableau::assert_lower(LraVarId var, DeltaRational value,
                                                               sat::Lit reason) {
  VarState& state = vars_.at(var);
  if (state.lower.has_value() && value <= state.lower->value) {
    return std::nullopt;
  }
  if (state.upper.has_value() && value > state.upper->value) {
    return std::vector<sat::Lit>{state.upper->reason, reason};
  }
  trail_.push_back(BoundTrailEntry{var, false, state.lower});
  state.lower = Bound{value, reason};
  if (!state.is_basic) {
    if (state.assignment < value) {
      update(var, value);
    }
  } else if (rule_ == PivotingRule::Soi) {
    set_soi_direction(var, soi_direction(var), rows_.at(var));
  }
  return std::nullopt;
}

void LraTableau::update(LraVarId nonbasic_var, const DeltaRational& new_value) {
  VarState& state = vars_.at(nonbasic_var);
  const DeltaRational delta = new_value - state.assignment;
  trail_.push_back(AssignmentTrailEntry{nonbasic_var, state.assignment});
  state.assignment = new_value;
  // Copy the affected-rows list before iterating: set_soi_direction()
  // below can mutate occurs_in_[some_var]'s vector (adding/removing
  // f_var_ from it) as a side effect, and if nonbasic_var itself happens
  // to be one of f_var_'s row's own terms, that mutation would hit this
  // exact occurs_in_[nonbasic_var] entry -- iterating the live reference
  // directly caused a real heap-use-after-free (found via manual ASAN
  // testing this round, not just theorized).
  const std::vector<LraVarId> affected = occurs_in_.at(nonbasic_var);
  for (const LraVarId basic_var : affected) {
    for (const auto& [var, coefficient] : rows_.at(basic_var)) {
      if (var == nonbasic_var) {
        VarState& basic_state = vars_.at(basic_var);
        trail_.push_back(AssignmentTrailEntry{basic_var, basic_state.assignment});
        basic_state.assignment = basic_state.assignment + delta.scaled(coefficient);
        // f_var_ itself is excluded: it has no bounds, so soi_direction()
        // is always 0 for it and the hook would be a no-op.
        if (rule_ == PivotingRule::Soi && (!f_var_.has_value() || basic_var != *f_var_)) {
          set_soi_direction(basic_var, soi_direction(basic_var), rows_.at(basic_var));
        }
        break;
      }
    }
  }
}

void LraTableau::pivot(LraVarId basic_var, LraVarId entering_var) {
  PivotTrailEntry trail_entry;
  trail_entry.basic_var = basic_var;
  trail_entry.entering_var = entering_var;
  trail_entry.old_basic_row = rows_.at(basic_var);

  // basic_var is about to become nonbasic and stop contributing to f_var_'s
  // row entirely -- its tracked direction must be reset to the nonbasic
  // baseline (0) now, using its row content as it stood BEFORE this
  // pivot's rewrite (trail_entry.old_basic_row, already copied above).
  // Without this, a variable that later re-enters the basis would compute
  // its next set_soi_direction() delta against a stale historical
  // direction instead of the correct nonbasic baseline. f_var_ can never
  // be basic_var here (it's never selected as a violated variable, having
  // no bounds), so no exclusion is needed.
  if (rule_ == PivotingRule::Soi) {
    set_soi_direction(basic_var, 0, trail_entry.old_basic_row);
  }

  // No explicit erase of basic_var's slot: it's about to become nonbasic,
  // and rebuild_occurs_in_index() (the only place that ever iterates all
  // of rows_ rather than a specific known-basic index) gates by is_basic,
  // not by content, so the moved-from (empty) leftover here is harmless.
  Row old_row = std::move(rows_.at(basic_var));

  util::Rational entering_coeff(0);
  Row remainder;
  remainder.reserve(old_row.size());
  for (const auto& [var, coeff] : old_row) {
    if (var == entering_var) {
      entering_coeff = coeff;
    } else {
      remainder.emplace_back(var, coeff);
    }
    auto& occurrences = occurs_in_[var];
    occurrences.erase(std::remove(occurrences.begin(), occurrences.end(), basic_var),
                      occurrences.end());
  }

  const util::Rational inv = util::Rational(1) / entering_coeff;
  Row new_entering_row;
  new_entering_row.reserve(remainder.size() + 1);
  new_entering_row.emplace_back(basic_var, inv);
  for (const auto& [var, coeff] : remainder) {
    new_entering_row.emplace_back(var, -(coeff * inv));
  }

  vars_.at(basic_var).is_basic = false;
  vars_.at(entering_var).is_basic = true;

  const std::vector<LraVarId> affected = occurs_in_[entering_var];
  occurs_in_[entering_var].clear();

  for (const LraVarId other_basic : affected) {
    Row other_row = rows_.at(other_basic);
    trail_entry.old_other_rows.emplace_back(other_basic, other_row);

    // Reused member (see combine_scratch_'s own comment): sort-merge
    // instead of a hash-based combine, avoiding per-element heap
    // allocation on every affected row of every pivot.
    combine_scratch_.clear();
    util::Rational entering_in_other(0);
    for (const auto& [var, coeff] : other_row) {
      if (var == entering_var) {
        entering_in_other = coeff;
        continue;
      }
      combine_scratch_.emplace_back(var, coeff);
      auto& occurrences = occurs_in_[var];
      occurrences.erase(std::remove(occurrences.begin(), occurrences.end(), other_basic),
                        occurrences.end());
    }
    for (const auto& [var, coeff] : new_entering_row) {
      combine_scratch_.emplace_back(var, entering_in_other * coeff);
    }
    std::sort(combine_scratch_.begin(), combine_scratch_.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

    Row rebuilt;
    rebuilt.reserve(combine_scratch_.size());
    std::size_t combine_index = 0;
    while (combine_index < combine_scratch_.size()) {
      const LraVarId var = combine_scratch_[combine_index].first;
      util::Rational sum = combine_scratch_[combine_index].second;
      ++combine_index;
      while (combine_index < combine_scratch_.size() &&
             combine_scratch_[combine_index].first == var) {
        sum += combine_scratch_[combine_index].second;
        ++combine_index;
      }
      if (sum.is_zero()) {
        continue;
      }
      rebuilt.emplace_back(var, sum);
      occurs_in_[var].push_back(other_basic);
    }
    rows_[other_basic] = std::move(rebuilt);
  }

  for (const auto& [var, coeff] : new_entering_row) {
    occurs_in_[var].push_back(entering_var);
  }
  rows_[entering_var] = std::move(new_entering_row);

  trail_.push_back(std::move(trail_entry));
}

std::vector<sat::Lit> LraTableau::build_conflict(LraVarId violated, bool too_low) const {
  const VarState& violated_state = vars_.at(violated);
  std::vector<sat::Lit> reasons;
  reasons.push_back(too_low ? violated_state.lower->reason : violated_state.upper->reason);
  for (const auto& [var, coeff] : rows_.at(violated)) {
    const VarState& var_state = vars_.at(var);
    const bool positive = coeff.sign() > 0;
    const bool want_upper = (too_low && positive) || (!too_low && !positive);
    reasons.push_back(want_upper ? var_state.upper->reason : var_state.lower->reason);
  }
  return reasons;
}

int LraTableau::soi_direction(LraVarId var) const {
  const VarState& state = vars_.at(var);
  if (state.lower.has_value() && state.assignment < state.lower->value) {
    return -1;
  }
  if (state.upper.has_value() && state.assignment > state.upper->value) {
    return 1;
  }
  return 0;
}

void LraTableau::set_soi_direction(LraVarId var, int new_direction, const Row& var_row) {
  int& stored = soi_directions_[var];
  if (new_direction == stored) {
    return;
  }
  const int delta = new_direction - stored;
  const int previous = stored;
  stored = new_direction;

  trail_.push_back(FRowTrailEntry{rows_.at(*f_var_), var, previous});

  // Reused member -- see combine_scratch_'s own comment.
  combine_scratch_.clear();
  for (const auto& [term_var, coeff] : rows_.at(*f_var_)) {
    combine_scratch_.emplace_back(term_var, coeff);
  }
  const util::Rational delta_rational(delta);
  for (const auto& [term_var, coeff] : var_row) {
    combine_scratch_.emplace_back(term_var, coeff * delta_rational);
  }
  std::sort(combine_scratch_.begin(), combine_scratch_.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

  Row& f_row = rows_.at(*f_var_);
  for (const auto& [term_var, coeff] : f_row) {
    auto& occurrences = occurs_in_[term_var];
    occurrences.erase(std::remove(occurrences.begin(), occurrences.end(), *f_var_),
                      occurrences.end());
  }
  Row rebuilt;
  rebuilt.reserve(combine_scratch_.size());
  std::size_t combine_index = 0;
  while (combine_index < combine_scratch_.size()) {
    const LraVarId term_var = combine_scratch_[combine_index].first;
    util::Rational sum = combine_scratch_[combine_index].second;
    ++combine_index;
    while (combine_index < combine_scratch_.size() &&
           combine_scratch_[combine_index].first == term_var) {
      sum += combine_scratch_[combine_index].second;
      ++combine_index;
    }
    if (sum.is_zero()) {
      continue;
    }
    rebuilt.emplace_back(term_var, sum);
    occurs_in_[term_var].push_back(*f_var_);
  }
  f_row = std::move(rebuilt);
}

namespace {
// One candidate crossing point for a single entering-variable walk
// (SOISELECT, Fig 5 + Section V-A): the point (as a signed delta from the
// entering variable's current assignment) at which row `k` (or the
// entering variable itself, if k==j) reaches one of its own bounds.
struct SoiBreakPoint {
  DeltaRational delta;
  LraVarId k;
};
}  // namespace

std::optional<LraTableau::SoiSelection> LraTableau::soi_select() const {
  const DeltaRational kZero = DeltaRational::from_rational(util::Rational(0));
  const Row& f_row = rows_.at(*f_var_);

  bool have_best = false;
  int best_sign = 0;
  util::Rational best_abs_score(0);
  LraVarId best_j = 0;
  SoiSelection best_selection{0, 0, kZero};

  for (const auto& [j, t0j] : f_row) {
    if (t0j.is_zero()) {
      continue;
    }
    const VarState& j_state = vars_.at(j);
    const bool t0j_positive = t0j.sign() > 0;
    // Flex(f) membership (eq 2, specialized to f's own row, implicit d=1):
    // decreasing j helps if t0j>0 and j isn't pinned at its lower bound;
    // increasing j helps if t0j<0 and j isn't pinned at its upper bound.
    const bool eligible =
        t0j_positive ? (!j_state.lower.has_value() || j_state.assignment > j_state.lower->value)
                    : (!j_state.upper.has_value() || j_state.assignment < j_state.upper->value);
    if (!eligible) {
      continue;
    }
    const bool delta_positive = !t0j_positive;  // walk direction that reduces f
    const int walk_sign = delta_positive ? 1 : -1;

    std::vector<SoiBreakPoint> points;
    // j's own bound -- caps how far the walk can go, contributes no beta
    // update (nonbasic variables are always within bounds, d_j stays 0).
    if (delta_positive) {
      if (j_state.upper.has_value()) {
        points.push_back(SoiBreakPoint{j_state.upper->value - j_state.assignment, j});
      }
    } else {
      if (j_state.lower.has_value()) {
        points.push_back(SoiBreakPoint{j_state.lower->value - j_state.assignment, j});
      }
    }
    // Every row k currently referencing j (excluding f_var_ itself, which
    // has no bounds and so contributes no breakpoint).
    for (const LraVarId k : occurs_in(j)) {
      if (k == *f_var_) {
        continue;
      }
      util::Rational coeff(0);
      for (const auto& [var, c] : rows_.at(k)) {
        if (var == j) {
          coeff = c;
          break;
        }
      }
      if (coeff.is_zero()) {
        continue;
      }
      const VarState& k_state = vars_.at(k);
      const util::Rational inv = util::Rational(1) / coeff;
      if (k_state.lower.has_value()) {
        const DeltaRational point_delta = (k_state.lower->value - k_state.assignment).scaled(inv);
        if ((delta_positive && point_delta >= kZero) || (!delta_positive && point_delta <= kZero)) {
          points.push_back(SoiBreakPoint{point_delta, k});
        }
      }
      if (k_state.upper.has_value()) {
        const DeltaRational point_delta = (k_state.upper->value - k_state.assignment).scaled(inv);
        if ((delta_positive && point_delta >= kZero) || (!delta_positive && point_delta <= kZero)) {
          points.push_back(SoiBreakPoint{point_delta, k});
        }
      }
    }
    if (points.empty()) {
      continue;
    }
    std::sort(points.begin(), points.end(), [&](const SoiBreakPoint& a, const SoiBreakPoint& b) {
      return delta_positive ? a.delta < b.delta : b.delta < a.delta;
    });
    // The entering variable's own bound caps the walk -- discard any
    // breakpoints beyond it (moving j past its own bound isn't a valid
    // UPDATE at all).
    for (std::size_t i = 0; i < points.size(); ++i) {
      if (points[i].k == j) {
        points.resize(i + 1);
        break;
      }
    }

    DeltaRational current_delta = kZero;
    util::Rational beta = t0j;
    DeltaRational delta_vio = kZero;
    bool have_best_for_j = false;
    DeltaRational best_delta_for_j = kZero;
    LraVarId best_k_for_j = j;
    DeltaRational best_delta_vio_for_j = kZero;

    std::size_t index = 0;
    while (index < points.size()) {
      const DeltaRational this_delta = points[index].delta;
      delta_vio = delta_vio + (this_delta - current_delta).scaled(beta);
      current_delta = this_delta;
      std::size_t group_end = index;
      while (group_end < points.size() && points[group_end].delta == this_delta) {
        const LraVarId k = points[group_end].k;
        const DeltaRational abs_delta = delta_positive ? this_delta : -this_delta;
        if (!have_best_for_j) {
          have_best_for_j = true;
          best_delta_for_j = this_delta;
          best_k_for_j = k;
          best_delta_vio_for_j = delta_vio;
        } else {
          const DeltaRational best_abs =
              delta_positive ? best_delta_for_j : -best_delta_for_j;
          if (delta_vio < best_delta_vio_for_j ||
              (delta_vio == best_delta_vio_for_j && abs_delta < best_abs) ||
              (delta_vio == best_delta_vio_for_j && abs_delta == best_abs && k < best_k_for_j)) {
            best_delta_for_j = this_delta;
            best_k_for_j = k;
            best_delta_vio_for_j = delta_vio;
          }
        }
        ++group_end;
      }
      // Update beta for the next segment: each k in this group transitions
      // direction as j crosses it (structurally determined by whether
      // k's assignment is increasing or decreasing through the crossing --
      // see the plan's derivation for why this collapses to +-1 regardless
      // of which specific bound is being crossed). j's own entry (k==j)
      // never contributes: nonbasic variables always satisfy their own
      // bounds, so d_j stays 0 through its own crossing.
      for (std::size_t g = index; g < group_end; ++g) {
        const LraVarId k = points[g].k;
        if (k == j) {
          continue;
        }
        util::Rational coeff(0);
        for (const auto& [var, c] : rows_.at(k)) {
          if (var == j) {
            coeff = c;
            break;
          }
        }
        const bool increasing = (coeff.sign() > 0) == (walk_sign > 0);
        const int delta_d = increasing ? 1 : -1;
        beta = beta + coeff * util::Rational(delta_d);
      }
      index = group_end;
    }
    if (!have_best_for_j) {
      continue;
    }

    const int sign = best_delta_vio_for_j == kZero ? 0 : (best_delta_vio_for_j < kZero ? -1 : 1);
    const util::Rational abs_score = t0j.sign() < 0 ? -t0j : t0j;
    if (!have_best) {
      have_best = true;
      best_sign = sign;
      best_abs_score = abs_score;
      best_j = j;
      best_selection = SoiSelection{j, best_k_for_j, best_delta_for_j};
    } else if (sign < best_sign || (sign == best_sign && abs_score > best_abs_score) ||
              (sign == best_sign && abs_score == best_abs_score && j < best_j)) {
      best_sign = sign;
      best_abs_score = abs_score;
      best_j = j;
      best_selection = SoiSelection{j, best_k_for_j, best_delta_for_j};
    }
  }

  if (!have_best) {
    return std::nullopt;
  }
  return best_selection;
}

// Section V-B's f_S = sum of d_i*row_i over only `subset` -- shared by
// build_soi_conflict (which needs f_S's own terms as conflict reasons)
// and soi_subset_still_conflicts (which needs Flex(f_S)'s eligibility
// check). Uses the CACHED soi_directions_ (not a fresh soi_direction()
// recomputation) since `subset` is always drawn from a known-error_vars
// list at a point where the cache is guaranteed consistent with live
// state (see set_soi_direction's invariant).
std::unordered_map<LraVarId, util::Rational> LraTableau::restricted_soi_row(
    const std::vector<LraVarId>& subset) const {
  std::unordered_map<LraVarId, util::Rational> restricted;
  for (const LraVarId basic_var : subset) {
    const int direction = soi_directions_.at(basic_var);
    if (direction == 0) {
      continue;
    }
    const util::Rational d(direction);
    for (const auto& [var, coeff] : rows_.at(basic_var)) {
      restricted[var] += coeff * d;
    }
  }
  return restricted;
}

std::vector<sat::Lit> LraTableau::build_soi_conflict(const std::vector<LraVarId>& error_vars) const {
  // Section V-B: f_S restricted to `error_vars` (NOT the full f_var_ row,
  // which reflects every originally-violated row -- error_vars may be a
  // minimize_soi_conflict()-shrunk subset of that), same sign-branching
  // pattern as build_conflict (f has no bounds of its own, so unlike
  // build_conflict there's no "too_low" direction to condition on --
  // positive coefficient always wants the lower-bound reason, negative
  // always wants upper).
  std::vector<sat::Lit> reasons;
  for (const auto& [var, coeff] : restricted_soi_row(error_vars)) {
    if (coeff.is_zero()) {
      continue;
    }
    const VarState& var_state = vars_.at(var);
    reasons.push_back(coeff.sign() > 0 ? var_state.lower->reason : var_state.upper->reason);
  }
  for (const LraVarId error_var : error_vars) {
    const VarState& state = vars_.at(error_var);
    const int direction = soi_directions_.at(error_var);
    reasons.push_back(direction < 0 ? state.lower->reason : state.upper->reason);
  }
  return reasons;
}

bool LraTableau::soi_subset_still_conflicts(const std::vector<LraVarId>& subset) const {
  // An empty subset is never a conflict (nothing is claimed violated, so
  // Vio(subset)=0 trivially) -- without this guard, f_empty is the zero
  // row, which vacuously has no eligible candidate and would otherwise
  // be (wrongly) treated as "stuck"/conflicting.
  if (subset.empty()) {
    return false;
  }
  const std::unordered_map<LraVarId, util::Rational> restricted = restricted_soi_row(subset);
  for (const auto& [j, t0j] : restricted) {
    if (t0j.is_zero()) {
      continue;
    }
    const VarState& j_state = vars_.at(j);
    const bool t0j_positive = t0j.sign() > 0;
    const bool eligible =
        t0j_positive ? (!j_state.lower.has_value() || j_state.assignment > j_state.lower->value)
                    : (!j_state.upper.has_value() || j_state.assignment < j_state.upper->value);
    if (eligible) {
      return false;
    }
  }
  return true;
}

// GCC 13 at -O3 mis-analyzes this function's recursive-inlining chain and
// emits a false-positive -Warray-bounds= on the `return candidates;` copy
// below (verified: the flagged copy is a plain, in-bounds vector copy
// construction, not an actual out-of-bounds access) -- suppressed
// locally rather than disabling the check project-wide.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds="
#pragma GCC diagnostic ignored "-Wstringop-overflow="
#endif
std::vector<LraVarId> LraTableau::quickxplain(const std::vector<LraVarId>& background,
                                              const std::vector<LraVarId>& candidates) const {
  if (candidates.empty()) {
    return {};
  }
  if (soi_subset_still_conflicts(background)) {
    return {};
  }
  if (candidates.size() == 1) {
    return candidates;
  }
  const auto half = static_cast<std::ptrdiff_t>(candidates.size() / 2);
  const std::vector<LraVarId> first(candidates.begin(), candidates.begin() + half);
  const std::vector<LraVarId> second(candidates.begin() + half, candidates.end());

  std::vector<LraVarId> background_with_first = background;
  background_with_first.insert(background_with_first.end(), first.begin(), first.end());
  const std::vector<LraVarId> second_result = quickxplain(background_with_first, second);

  std::vector<LraVarId> background_with_second_result = background;
  background_with_second_result.insert(background_with_second_result.end(), second_result.begin(),
                                       second_result.end());
  const std::vector<LraVarId> first_result = quickxplain(background_with_second_result, first);

  std::vector<LraVarId> combined = first_result;
  combined.insert(combined.end(), second_result.begin(), second_result.end());
  return combined;
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

std::vector<LraVarId> LraTableau::minimize_soi_conflict(const std::vector<LraVarId>& error_vars) const {
  return quickxplain({}, error_vars);
}

std::optional<std::vector<sat::Lit>> LraTableau::check() {
  // Anti-cycling fallback for PivotingRule::Dantzig (see the PivotingRule
  // class comment): once more than this many pivots have happened within
  // this call, switch to Bland's-rule selection (both loops below) for
  // the remainder. A heuristic threshold, not a formally derived bound --
  // generous relative to the tableau size, not tuned. Reset every call
  // (cycling risk is per Check/Pivot episode, not global across the
  // tableau's lifetime).
  const std::size_t bland_fallback_threshold = 4 * vars_.size() + 64;
  std::size_t pivot_count = 0;
  bool fallback_counted = false;

  if (rule_ == PivotingRule::Soi) {
    // SOICHECK (Fig 5): note the nonbasic-in-error fixup (SOICHECK line 1)
    // is unconditionally already an invariant here -- assert_upper/
    // assert_lower always sync a nonbasic variable's assignment
    // immediately, so N∩E is always empty by construction; nothing to do.
    while (pivot_count <= bland_fallback_threshold) {
      const auto selection = soi_select();
      if (!selection.has_value()) {
        // Flex(f) empty (Section V/V-B): either satisfiable (no basic
        // variable remains violated) or a genuine multi-row conflict.
        std::vector<LraVarId> error_vars;
        for (LraVarId id = 0; id < vars_.size(); ++id) {
          if (vars_[id].is_basic && soi_direction(id) != 0) {
            error_vars.push_back(id);
          }
        }
        if (error_vars.empty()) {
          return std::nullopt;
        }
        ++soi_multi_row_conflict_count_;
        const std::vector<LraVarId> minimized = minimize_soi_conflict(error_vars);
        if (minimized.size() < error_vars.size()) {
          ++soi_quickxplain_call_count_;
        }
        return build_soi_conflict(minimized);
      }
      if (selection->entering == selection->leaving) {
        // j reaches its own bound directly -- UPDATE without a pivot
        // (PRIMAL's own footnote: k==j collapses UPDATEANDPIVOT to a
        // plain update).
        update(selection->entering, vars_.at(selection->entering).assignment + selection->delta);
      } else {
        // Capture the leaving row's coefficient for `entering` before
        // pivot() rewrites it, to compute the leaving variable's exact
        // target value directly -- reuses the existing Bland/Dantzig
        // convention below (pivot first using the old row shape, then
        // update the now-nonbasic leaving variable; its occurs_in_ entry
        // post-pivot correctly includes the new entering row).
        util::Rational coeff(0);
        for (const auto& [var, c] : rows_.at(selection->leaving)) {
          if (var == selection->entering) {
            coeff = c;
            break;
          }
        }
        const DeltaRational target =
            vars_.at(selection->leaving).assignment + selection->delta.scaled(coeff);
        pivot(selection->leaving, selection->entering);
        update(selection->leaving, target);
      }
      ++pivot_count;
    }
    // Defensive fallback, belt-and-suspenders beyond the paper's own
    // tie-breaking-reduces-to-Bland termination argument (Section V-C):
    // switch to plain Bland selection for the remainder by falling
    // through to the loop below -- pivot_count already exceeds the
    // threshold, so that loop's existing `use_bland` check (`rule_ ==
    // PivotingRule::Bland || pivot_count > bland_fallback_threshold`)
    // already selects Bland unconditionally from here on, with no
    // changes needed to that loop itself. f_var_ is never violated (no
    // bounds), so its ordinary violated-variable scan stays correct too.
    ++bland_fallback_count_;
  }

  while (true) {
    if (rule_ == PivotingRule::Dantzig && !fallback_counted && pivot_count > bland_fallback_threshold) {
      ++bland_fallback_count_;
      fallback_counted = true;
    }
    const bool use_bland = rule_ == PivotingRule::Bland || pivot_count > bland_fallback_threshold;

    LraVarId violated = vars_.size();
    bool too_low = false;
    DeltaRational best_violation;
    for (LraVarId id = 0; id < vars_.size(); ++id) {
      const VarState& state = vars_[id];
      if (!state.is_basic) {
        continue;
      }
      bool candidate_too_low = false;
      bool is_violated = false;
      DeltaRational violation;
      if (state.lower.has_value() && state.assignment < state.lower->value) {
        is_violated = true;
        candidate_too_low = true;
        violation = state.lower->value - state.assignment;
      } else if (state.upper.has_value() && state.assignment > state.upper->value) {
        is_violated = true;
        candidate_too_low = false;
        violation = state.assignment - state.upper->value;
      }
      if (!is_violated) {
        continue;
      }
      if (use_bland) {
        violated = id;
        too_low = candidate_too_low;
        break;
      }
      // Dantzig-style: pick the most-violated basic variable (largest
      // magnitude out-of-bounds), tie-breaking by smallest index.
      if (violated == vars_.size() || violation > best_violation) {
        violated = id;
        too_low = candidate_too_low;
        best_violation = violation;
      }
    }
    if (violated == vars_.size()) {
      return std::nullopt;
    }

    LraVarId entering = vars_.size();
    util::Rational best_abs_coeff(0);
    for (const auto& [var, coeff] : rows_.at(violated)) {
      const VarState& var_state = vars_.at(var);
      const bool positive = coeff.sign() > 0;
      const bool can_move =
          too_low ? (positive ? (!var_state.upper.has_value() || var_state.assignment < var_state.upper->value)
                              : (!var_state.lower.has_value() || var_state.assignment > var_state.lower->value))
                 : (positive ? (!var_state.lower.has_value() || var_state.assignment > var_state.lower->value)
                              : (!var_state.upper.has_value() || var_state.assignment < var_state.upper->value));
      if (!can_move) {
        continue;
      }
      if (use_bland) {
        if (var < entering) {
          entering = var;
        }
        continue;
      }
      // Dantzig-style: pick the largest |coefficient| among eligible
      // entering candidates, tie-breaking by smallest index -- Row isn't
      // sorted by var id, so the index comparison has to be explicit
      // (can't rely on scan order the way the Bland branch above does).
      const util::Rational abs_coeff = coeff.sign() < 0 ? -coeff : coeff;
      if (entering == vars_.size() || abs_coeff > best_abs_coeff ||
          (abs_coeff == best_abs_coeff && var < entering)) {
        entering = var;
        best_abs_coeff = abs_coeff;
      }
    }
    if (entering == vars_.size()) {
      return build_conflict(violated, too_low);
    }

    const DeltaRational target =
        too_low ? vars_.at(violated).lower->value : vars_.at(violated).upper->value;
    pivot(violated, entering);
    update(violated, target);
    ++pivot_count;
  }
}

void LraTableau::pop(std::size_t checkpoint) {
  while (trail_.size() > checkpoint) {
    undo(trail_.back());
    trail_.pop_back();
  }
}

void LraTableau::undo(const TrailEntry& entry) {
  if (const auto* bound = std::get_if<BoundTrailEntry>(&entry)) {
    VarState& state = vars_.at(bound->var);
    if (bound->is_upper) {
      state.upper = bound->previous;
    } else {
      state.lower = bound->previous;
    }
    return;
  }
  if (const auto* assignment = std::get_if<AssignmentTrailEntry>(&entry)) {
    vars_.at(assignment->var).assignment = assignment->previous;
    return;
  }
  if (const auto* pivot_entry = std::get_if<PivotTrailEntry>(&entry)) {
    // No explicit clear of entering_var's slot: it's about to become
    // nonbasic again, and rebuild_occurs_in_index() below is is_basic-
    // gated, not content-gated (see rows_'s own comment), so the stale
    // leftover here is harmless.
    rows_[pivot_entry->basic_var] = pivot_entry->old_basic_row;
    vars_.at(pivot_entry->basic_var).is_basic = true;
    vars_.at(pivot_entry->entering_var).is_basic = false;
    for (const auto& [other_basic, old_row] : pivot_entry->old_other_rows) {
      rows_[other_basic] = old_row;
    }
    rebuild_occurs_in_index();
    return;
  }
  // PivotingRule::Soi only: undo one set_soi_direction() call's effect,
  // restoring both f_var_'s row and the affected variable's tracked
  // direction atomically (see FRowTrailEntry's comment).
  const auto& f_row_entry = std::get<FRowTrailEntry>(entry);
  rows_[*f_var_] = f_row_entry.previous_f_row;
  soi_directions_[f_row_entry.var] = f_row_entry.previous_direction;
  rebuild_occurs_in_index();
}

void LraTableau::rebuild_occurs_in_index() {
  // rows_ has a slot for every variable, not just currently-basic ones
  // (see rows_'s own comment) -- a nonbasic slot's leftover content must
  // not be walked here, hence the explicit is_basic gate rather than
  // iterating rows_ directly.
  for (std::vector<LraVarId>& entry : occurs_in_) {
    entry.clear();
  }
  for (LraVarId basic_var = 0; basic_var < vars_.size(); ++basic_var) {
    if (!vars_[basic_var].is_basic) {
      continue;
    }
    for (const auto& [var, coefficient] : rows_[basic_var]) {
      occurs_in_[var].push_back(basic_var);
    }
  }
}

}  // namespace primo::theory::arithmetic
