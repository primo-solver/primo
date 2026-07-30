#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "util/statistics.hpp"

namespace primo::smt {

enum class QfUfPreprocessMode {
  Off,
  Basic,
  Strong,
  Auto,
};

enum class EufPropagationMode {
  Auto,
  Off,
  Conflict,
  Model,
  Online,
};

enum class EufEngine {
  Lazy,
  Incremental,
};

enum class PureTheoryFilterMode {
  Auto,
  On,
  Off,
};

enum class TheoryPhaseMode {
  Negative,
  Positive,
  None,
  Polarity,
  Auto,
};

enum class EufExplainSearchMode {
  OneSidedBfs,
  BidirectionalBfs,
  ProofForest,
  ReorientedProofForest,
  StructuralProofForest,
};

enum class QfUfPortfolioMode {
  Off,
  BasicPolarity,
};

// Mirrors theory::arithmetic::LraTableau::PivotingRule -- kept as a
// separate check_sat.hpp-local enum (translated at the wiring point, see
// lra_pivoting_rule_from_options in check_sat.cpp) rather than reused
// directly, the same way EufExplainSearchMode is distinct from
// theory::equality::FlatExplanationSearchMode -- keeps this header free
// of theory-layer includes.
enum class LraPivotingRule {
  Bland,
  Dantzig,
  Soi,
  Sparse,
};

struct AutoBudget {
  enum class Kind {
    Auto,
    Unlimited,
    Fixed,
  };

  Kind kind = Kind::Auto;
  std::size_t value = 0;
};

enum class CheckSatResult {
  Sat,
  Unsat,
  Unknown,
  Unsupported,
};

struct CheckSatOutcome {
  CheckSatResult result = CheckSatResult::Unknown;
  std::string message;
  util::Statistics statistics;
};

struct CheckSatOptions {
  QfUfPreprocessMode qf_uf_preprocess = QfUfPreprocessMode::Auto;
  EufEngine euf_engine = EufEngine::Incremental;
  EufPropagationMode euf_propagation = EufPropagationMode::Auto;
  PureTheoryFilterMode pure_theory_filter = PureTheoryFilterMode::Auto;
  TheoryPhaseMode theory_phase = TheoryPhaseMode::Negative;
  EufExplainSearchMode euf_explain_search = EufExplainSearchMode::OneSidedBfs;
  bool euf_proof_forest_diagnostics = false;
  AutoBudget euf_propagation_budget;
  AutoBudget euf_propagation_delivery{AutoBudget::Kind::Fixed, 1000};
  // QF_LRA same-slack bound propagation (Milestone 3b: Dutertre & de Moura
  // Section 4.2's "deriving satisfied atoms"). Unlike euf_propagation,
  // this is a plain on/off performance switch, not a multi-mode choice --
  // the propagation check is exact and cheap (O(atoms sharing a slack)),
  // so there's no heuristic budget/backoff tuning to expose.
  bool lra_propagation = true;
  // Row-based/interval bound propagation (Dutertre & de Moura Section 4,
  // generalizing lra_propagation's same-slack case to derive an implied
  // bound for a row from its terms' current bounds). Only takes effect
  // when lra_propagation is also enabled. Opt-in (materially new
  // algorithmic machinery, not yet campaign-fuzzed) -- see
  // docs/arithmetic.md's Milestone roadmap.
  bool lra_row_propagation = false;
  // Competitiveness-roadmap item 7: the reverse direction of
  // lra_row_propagation -- tighten one of a row's *other* terms from the
  // row's own basic-variable bound plus its remaining terms, rather than
  // only deriving the row's own implied bound. Only takes effect when
  // lra_row_propagation is also enabled. Its own flag (not folded into
  // lra_row_propagation) so the two directions can be A/B tested
  // independently -- see docs/arithmetic.md's Milestone roadmap.
  bool lra_bidirectional_row_propagation = false;
  // Competitiveness-roadmap item 8: theory-guided decisions via CaDiCaL's
  // cb_decide -- the arithmetic theory actively ranks and supplies a
  // decision literal on (almost) every decision, rather than leaving every
  // decision to CaDiCaL's generic VSIDS. Independent of lra_propagation/
  // lra_row_propagation (it fires at decision time, not propagation time),
  // so it has its own flag rather than depending on either -- see
  // docs/arithmetic.md's Milestone roadmap.
  bool lra_theory_decisions = false;
  // Default since 2026-07-29 (heuristics-note item H3). On the 237-file
  // stratified sample this solves 168 against Bland's 144, PAR2 1538.9s
  // against 1962.4s, with zero regressions and zero wrong answers -- see
  // docs/arithmetic.md. Bland remains reachable via --lra-pivoting-rule
  // bland, and check() still falls back to Bland selection past a per-call
  // pivot threshold for termination.
  LraPivotingRule lra_pivoting_rule = LraPivotingRule::Sparse;
  // Only meaningful with LraPivotingRule::Sparse -- see
  // LraTableau::set_sparse_leaving. Default-on with it: the entering-side
  // rule alone reaches 154/237, this takes it to 168, and it is the whole of
  // the latendresse result (0/15 -> 6/15, matching z3).
  bool lra_sparse_leaving = true;
  // Heuristics-note item H7 -- see LraTableau::set_fixed_elimination. Pivots
  // a basic variable out of the basis as soon as its bounds coincide, and
  // freezes the resulting row when it constrains nothing. Independent of the
  // pivoting rule. Off via --no-lra-fixed-elimination, which is also the
  // ablation switch the H7 measurements are stated against.
  bool lra_fixed_elimination = true;
  // The freezing half of item H7 -- see LraTableau::set_freeze_eliminated_rows.
  // Separate from lra_fixed_elimination because the elimination pivot is a
  // pure representation change while freezing is the part that can be
  // unsound, so it gets its own ablation switch
  // (--no-lra-freeze-eliminated-rows).
  bool lra_freeze_eliminated_rows = true;
  bool uf_symmetry_breaking = true;
  bool finite_domain_value_precedence = true;
  bool finite_domain_equality_definitions = true;
  bool finite_domain_post_amo = true;
  bool auto_finite_domain_equality_definitions = false;
  bool eq_bridge = true;
  bool ground_congruence = false;
  bool ground_unit_substitution = false;
  bool guarded_ground_unit_substitution = false;
  bool solved_constant_substitution = true;
  bool gaussian_elimination = true;
  bool top_level_or_tseitin = false;
  bool auto_top_level_or_tseitin = true;
  bool auto_basic_single_preprocessing = true;
  bool auto_basic_single_predicate_polarity = true;
  bool auto_basic_multi_preprocessing = true;
  bool auto_basic_multi_polarity = true;
  QfUfPortfolioMode qf_uf_portfolio = QfUfPortfolioMode::Off;
  bool preprocess_only = false;
  std::size_t euf_propagation_interval = 32;
  double euf_propagation_assign_threshold = 0.25;
  std::size_t preprocessing_passes = 4;
  std::size_t preprocessing_adaptive_extra_passes = 4;
  std::size_t preprocessing_adaptive_shrink_percent = 10;
  std::size_t finite_domain_eqdefs_auto_domain_limit = 8;
  std::size_t finite_domain_eqdefs_auto_clause_limit = 5000;
  std::size_t finite_domain_eqdefs_auto_term_limit = 5000;
  std::optional<std::size_t> max_theory_conflicts;
  std::size_t qf_uf_portfolio_conflicts = 2000;
  std::vector<std::pair<std::string, int>> cadical_options;
  util::Statistics* live_statistics = nullptr;
};

CheckSatOutcome check_sat_smtlib(std::string_view input, CheckSatOptions options = {});
std::string_view to_string(CheckSatResult result);

// Copies the currently in-flight solve's live sat.* counters (decisions,
// conflicts, propagations, ...) into its Statistics object, if a solve is
// in flight. CaDiCaL's own counters (see sat::IpasirUpBackend::statistics())
// are safe to read at any time; what this exists to fix is that check_sat's
// own copy into the caller-visible Statistics only happens synchronously
// immediately before and after each blocking solve() call, so a timeout
// signal handler firing mid-solve would otherwise report whatever was last
// captured before that call started (often all zero, on the very first
// solve() of a run) instead of the search's actual current progress.
// Intended to be called from a SIGTERM/timeout handler right before
// printing --stats output; a no-op if no solve is currently in flight.
void refresh_active_solve_stats();

}  // namespace primo::smt
