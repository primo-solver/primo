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

enum class ModelEqualityBranchPolicy {
  First,
  Ranked,
  Adaptive,
  CareQueued,
};

// Which formulas reach the mixed EUF/LRA solver rather than the pure QF_LRA
// one. This is a *dispatch* policy, not a theory setting.
//
// `Narrow` (default): only a Bool-sorted Application counts as an EUF
// predicate, so a formula that is pure QF_LRA apart from propositional
// variables stays on the pure path.
//
// `Wide`: a Bool-sorted Constant counts too -- the behaviour before
// 2026-08-07. That sends any QF_LRA formula containing a single propositional
// variable to the mixed solver, which is usually much worse (it hands those
// variables to congruence closure) but is genuinely better on some instances.
// See docs/nelson-oppen-qf-lra-regression.md and open-leads lead 2b: neither
// path dominates.
//
// Kept reachable for two reasons beyond curiosity: it is a portfolio dimension,
// and it lets differential fuzzing exercise the mixed path on inputs that would
// otherwise never reach it.
enum class MixedDispatchMode {
  Narrow,
  Wide,
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

// Defensive caps on row-based/interval bound propagation's per-bound-change
// cost. Unlike same-slack propagation (O(atoms sharing a slack), typically
// small), a row-based scan is triggered by *any* bound change on a variable
// that may occur in many rows, and the reverse direction runs an O(row size)
// child-bound computation for *each* other term of each affected row --
// multiplicatively pricier, which is why its two caps default smaller.
//
// The defaults are not derived from theory; they were picked to be generous
// enough to matter on real formulas and small enough to bound worst-case cost
// on adversarial/fuzzed input (the Gaussian-elimination caps' own precedent).
// They are options rather than constants because measurement says the
// defaults are the binding constraint, not the mechanism: on the 500KB-5MB
// gap band the scan fires 11-68 times against a 7k-190k opportunity count,
// and raising the caps to 256 makes scrambled312090 -- a file that times out
// at 60s by default -- solve in about 58s. See docs/qf-lra-heuristics.md
// section 6.5 (direction D1); the sweep that settles the defaults needs to
// vary them per run, not per build.
// Mirrors theory::arithmetic::LraTableau::Tuning, translated at the same
// wiring point as LraPivotingRule and for the same reason -- this header
// stays free of theory-layer includes. That header documents the defaults
// and the argument that every value in range is sound (these knobs change
// how long an answer takes, never which answer it is).
//
// Hardcoded constants until 2026-08-06. Exposed so the parameter-tuning
// campaign can vary them per run rather than per build; the defaults are
// exactly the constants they replaced, so an untuned run is unchanged.
// See docs/tuning-parameters.md.
// Mirrors theory::arithmetic::LraTableau::SoiMinimization /
// ::SoiMinimizationOrder, translated at the wiring point the same way
// LraPivotingRule is -- keeps this header free of theory-layer includes.
enum class LraSoiMinimization { QuickXplain, Deletion, None };
enum class LraSoiMinimizationOrder { Id, RowSupport, Violation };

struct LraTuning {
  std::size_t sparse_pricing_candidates = 16;
  std::size_t bland_fallback_factor = 4;
  std::size_t bland_fallback_offset = 64;
  LraSoiMinimization soi_minimization = LraSoiMinimization::QuickXplain;
  LraSoiMinimizationOrder soi_minimization_order = LraSoiMinimizationOrder::Id;
};

struct LraRowPropagationLimits {
  std::size_t max_row_size = 64;
  std::size_t max_fanout = 64;
  std::size_t bidirectional_max_row_size = 32;
  std::size_t bidirectional_max_fanout = 32;
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

enum class NelsonOppenPropagationMode {
  Exact,
  Final,
  Online,
};

struct CheckSatOutcome {
  CheckSatResult result = CheckSatResult::Unknown;
  std::string message;
  util::Statistics statistics;
  std::string model;
};

struct CheckSatOptions {
  QfUfPreprocessMode qf_uf_preprocess = QfUfPreprocessMode::Auto;
  EufEngine euf_engine = EufEngine::Incremental;
  EufPropagationMode euf_propagation = EufPropagationMode::Auto;
  PureTheoryFilterMode pure_theory_filter = PureTheoryFilterMode::Auto;
  TheoryPhaseMode theory_phase = TheoryPhaseMode::Negative;
  EufExplainSearchMode euf_explain_search = EufExplainSearchMode::OneSidedBfs;
  bool euf_proof_forest_diagnostics = false;
  bool euf_diseq_propagation_diagnostics = false;
  bool euf_ackermann_diagnostics = false;
  bool nelson_oppen_diagnostics = false;
  bool nelson_oppen_model_equality_branching = false;
  bool nelson_oppen_model_equality_mutation = true;
  ModelEqualityBranchPolicy nelson_oppen_model_equality_branch_policy =
      ModelEqualityBranchPolicy::First;
  NelsonOppenPropagationMode nelson_oppen_propagation = NelsonOppenPropagationMode::Final;
  bool euf_dynamic_ackermann_shadow = false;
  bool euf_diseq_ackermann = false;
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
  // Cost caps for the two row-propagation directions above -- see
  // LraRowPropagationLimits. Inert unless lra_row_propagation is on.
  LraRowPropagationLimits lra_row_propagation_limits;
  // Competitiveness-roadmap item 8: theory-guided decisions via CaDiCaL's
  // cb_decide -- the arithmetic theory actively ranks and supplies a
  // decision literal on (almost) every decision, rather than leaving every
  // decision to CaDiCaL's generic VSIDS. Independent of lra_propagation/
  // lra_row_propagation (it fires at decision time, not propagation time),
  // so it has its own flag rather than depending on either -- see
  // docs/arithmetic.md's Milestone roadmap.
  bool lra_theory_decisions = false;
  // H5 model-based phase selection: keep CaDiCaL's VSIDS variable choice
  // but refresh arithmetic atom polarities from the online tableau's current
  // assignment at quiescence points. Pure performance hint; default off until
  // tropical/sc/uart and broad-regression measurements justify promotion.
  bool lra_model_phase = false;
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
  bool lra_least_violated_leaving = false;
  // Numeric Check/Pivot knobs -- see LraTuning.
  LraTuning lra_tuning;
  // See MixedDispatchMode. Narrow keeps pure QF_LRA on the pure path.
  MixedDispatchMode mixed_dispatch = MixedDispatchMode::Narrow;
  // Heuristics-note item H7 -- see LraTableau::set_fixed_elimination.
  // Opt-in (--lra-fixed-elimination): on the 595-file scrambled competition
  // set it solves 374 against 373 with it off, a median 0.94x speedup over
  // commonly-solved files, and +19/-18 churn that nets to nothing. Its one
  // real effect is on the largest instances (>5MB: 7 -> 14 of 50), paid for
  // in the 50-500KB band. Worth enabling on equality-dominated input.
  bool lra_fixed_elimination = false;
  // Fixed-variable elimination is part of the mixed convex equality-sharing
  // path by default. It exposes EqRow consequences and reduces exact fallback
  // probes without changing the pure QF_LRA default above. The mixed setting
  // remains independently disableable for ablation.
  bool qf_uflra_fixed_elimination = true;
  bool uf_symmetry_breaking = true;
  bool finite_domain_value_precedence = true;
  bool finite_domain_equality_definitions = true;
  bool finite_domain_post_amo = true;
  bool auto_finite_domain_equality_definitions = false;
  bool eq_bridge = true;
  bool ground_congruence = false;
  bool ground_unit_substitution = false;
  bool guarded_ground_unit_substitution = false;
  bool guarded_real_equality_lowering = false;
  bool monotone_elimination = false;
  bool solved_constant_substitution = true;
  bool gaussian_elimination = true;
  bool qf_uflra_candidate_final_scan = false;
  // Default Section 4 shared-model reconciliation for convex QF_UFLRA.
  // LRA-entailed equalities that EUF can absorb are retained in the round's
  // EUF model instead of being materialized as SAT atoms. The explained exact
  // path remains available explicitly and as the conservative fallback for
  // failed candidate invariants.
  bool qf_uflra_model_reconciliation = true;
  bool qf_uflra_online_euf = false;
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
  std::size_t euf_diseq_ackermann_max_width = 3;
  std::size_t euf_diseq_ackermann_queue_budget = 4096;
  std::size_t euf_diseq_ackermann_per_symbol = 64;
  std::size_t nelson_oppen_model_equality_branch_budget = 16;
  // Optional associative Boolean preprocessing. Zero preserves the core DAG;
  // positive values bound arity growth caused by flattening.
  std::size_t boolean_flatten_threshold = 0;
  // Same pass, applied again AFTER the rewriting preprocessing passes.
  //
  // Why a second application point: since the DAG repair
  // (docs/boolean-dag-flattening.md) TermManager::mk_and / mk_or no longer
  // flatten same-kind children at construction. The rewriting passes build
  // fresh nodes through those, so they reintroduce nesting that the input
  // never had -- and the pre-pass above has already run by then, which is why
  // it reports attempts=0 on inputs that nonetheless end up nested.
  //
  // Default 2048, chosen by measurement rather than intuition. 256 restores
  // most QF_LRA inputs but leaves one of 55 short (20,200 threshold stops on
  // simple-scaled200), and raising the bound costs nothing where the bound
  // exists to protect: on the 94 MB cpachecker-induction.cs_fib instance that
  // motivated the DAG repair, 2048 peaks at 3,346,944 KB against 3,347,208 KB
  // at 256 -- a 264 KB *saving*, with threshold stops falling from 17 to 1.
  //
  // The bound still matters; it is what keeps a pathological input from
  // unbounded arity growth. It is simply not the axis that decides memory on
  // real inputs, because the pass rewrites each reachable node once rather
  // than materialising every prefix. Set 0 to disable.
  std::size_t boolean_flatten_post_threshold = 2048;
  std::size_t preprocessing_passes = 4;
  std::size_t preprocessing_adaptive_extra_passes = 4;
  std::size_t preprocessing_adaptive_shrink_percent = 10;
  std::size_t finite_domain_eqdefs_auto_domain_limit = 8;
  std::size_t finite_domain_eqdefs_auto_clause_limit = 5000;
  std::size_t finite_domain_eqdefs_auto_term_limit = 5000;
  std::optional<std::size_t> nelson_oppen_arrangement_probe_limit;
  std::optional<std::size_t> max_theory_conflicts;
  std::size_t qf_uf_portfolio_conflicts = 2000;
  std::vector<std::pair<std::string, int>> cadical_options;
  util::Statistics* live_statistics = nullptr;
  bool produce_model = false;
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
