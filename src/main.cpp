#include <unistd.h>

#include <CLI/CLI.hpp>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "generated/primo_git_revision.hpp"
#include "sat/cadical_backend.hpp"
#include "smt/check_sat.hpp"
#include "smt/preprocessing.hpp"
#include "util/statistics.hpp"

namespace {

primo::util::Statistics g_live_statistics;
bool g_print_timeout_statistics = false;

void handle_sigterm(int) {
  [[maybe_unused]] const ssize_t written = write(STDOUT_FILENO, "unknown\n", 8);
  if (g_print_timeout_statistics) {
    // Without this, sat.decisions/sat.conflicts/sat.propagations read
    // whatever was captured before the in-flight solve() call started
    // (often all zero) -- solve() is blocking, so check_sat's own copy
    // into g_live_statistics only happens synchronously around each call,
    // not while one is running. See check_sat.hpp's comment.
    primo::smt::refresh_active_solve_stats();
    primo::util::print_statistics(std::cerr, g_live_statistics);
  }
  std::exit(0);
}

bool can_read_file(const std::string& path) {
  std::ifstream input(path);
  return input.good();
}

std::string read_file(const std::string& path) {
  std::ifstream input(path);
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

std::string version_text() {
  std::ostringstream version;
  version << "primo 0.1.0\n";
  version << "git: " << PRIMO_GIT_REVISION << "\n";
  version << "build: " << PRIMO_BUILD_TYPE << ", sanitizers: " << PRIMO_SANITIZERS_BANNER << "\n";
  version << "cadical: " << primo::sat::cadical_signature();
  return version.str();
}

void print_startup_banner() {
  std::cerr << "primo git " << PRIMO_GIT_REVISION << " (" << PRIMO_BUILD_TYPE
            << ", sanitizers: " << PRIMO_SANITIZERS_BANNER << ")\n";
  std::cerr << "cadical " << primo::sat::cadical_signature() << "\n";
}

void print_outcome(const primo::smt::CheckSatOutcome& outcome, bool print_stats) {
  std::cout << primo::smt::to_string(outcome.result) << "\n";
  if (!outcome.model.empty()) {
    std::cout << outcome.model << "\n";
  }
  if (print_stats) {
    primo::util::print_statistics(std::cerr, outcome.statistics);
  }
}

std::optional<primo::smt::AutoBudget> parse_auto_budget(const std::string& text) {
  primo::smt::AutoBudget budget;
  if (text == "auto") {
    budget.kind = primo::smt::AutoBudget::Kind::Auto;
    return budget;
  }
  if (text == "unlimited") {
    budget.kind = primo::smt::AutoBudget::Kind::Unlimited;
    return budget;
  }
  try {
    std::size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed);
    if (consumed != text.size()) {
      return std::nullopt;
    }
    if (value > std::numeric_limits<std::size_t>::max()) {
      return std::nullopt;
    }
    budget.kind = primo::smt::AutoBudget::Kind::Fixed;
    budget.value = static_cast<std::size_t>(value);
    return budget;
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::optional<std::pair<std::string, int>> parse_cadical_option(const std::string& text) {
  const std::size_t separator = text.find('=');
  if (separator == std::string::npos || separator == 0 || separator + 1 == text.size()) {
    return std::nullopt;
  }
  try {
    std::size_t consumed = 0;
    const long value = std::stol(text.substr(separator + 1), &consumed);
    if (consumed != text.size() - separator - 1) {
      return std::nullopt;
    }
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
      return std::nullopt;
    }
    return std::pair{text.substr(0, separator), static_cast<int>(value)};
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// --help section headings. CLI11 emits sections in order of the first
// option registered into each, so the printed order follows registration
// order in main() -- currently Procedural, QF_UF, QF_LRA, Preprocessing,
// SAT -- not the order of these declarations.
constexpr const char* kGroupLra = "QF_LRA theory options";
constexpr const char* kGroupUf = "QF_UF / EUF theory options";
constexpr const char* kGroupPreprocessing = "Preprocessing options";
constexpr const char* kGroupSat = "SAT and DPLL(T) options";
constexpr const char* kGroupProcedural = "Procedural options";

}  // namespace

int main(int argc, char** argv) {
  std::signal(SIGTERM, handle_sigterm);
  std::vector<std::string> files;
  bool quiet = false;
  bool print_stats = false;
  bool no_uf_symmetry_breaking = false;
  bool no_finite_domain_value_precedence = false;
  bool finite_domain_equality_definitions = false;
  bool no_finite_domain_equality_definitions = false;
  bool no_finite_domain_post_amo = false;
  bool no_eq_bridge = false;
  bool no_lra_propagation = false;
  bool no_solved_constant_substitution = false;
  bool no_gaussian_elimination = false;
  bool no_qf_uflra_fixed_elimination = false;
  bool qf_uflra_candidate_final_scan = false;
  bool qf_uflra_model_reconciliation = false;
  bool no_qf_uflra_model_reconciliation = false;
  bool qf_uflra_online_euf = false;
  bool guarded_real_equality_lowering = false;
  bool monotone_elimination = false;
  bool top_level_or_tseitin = false;
  bool no_top_level_or_tseitin = false;
  bool no_auto_top_level_or_tseitin = false;
  bool no_auto_basic_single = false;
  bool no_auto_basic_single_predicate_polarity = false;
  bool auto_basic_multi = false;
  bool no_auto_basic_multi = false;
  bool no_auto_basic_multi_polarity = false;
  std::string preprocessing_mode = "auto";
  std::string deprecated_qf_uf_preprocessing_mode;
  std::string auto_preprocess_selector = "legacy";
  std::string deprecated_qf_uf_auto_selector;
  std::string boolean_implies_rewrite = "off";
  std::string qf_uf_profile = "default";
  std::string qf_uf_portfolio = "off";
  std::string euf_engine = "incremental";
  std::string euf_prop = "auto";
  std::string euf_explain_search = "one-sided";
  std::string lra_pivoting_rule = "sparse";
  std::string lra_soi_minimize = "quickxplain";
  std::string lra_soi_minimize_order = "id";
  std::string mixed_dispatch = "narrow";
  std::string qf_uflra_lra_equality_producer = "legacy";
  bool no_lra_sparse_leaving = false;
  bool lra_least_violated_leaving = false;
  bool euf_proof_forest_diagnostics = false;
  bool euf_diseq_propagation_diagnostics = false;
  bool euf_ackermann_diagnostics = false;
  bool nelson_oppen_diagnostics = false;
  bool nelson_oppen_model_equality_branching = false;
  bool no_nelson_oppen_model_equality_mutation = false;
  std::string model_equality_mutation_scope = "all";
  std::string nelson_oppen_propagation = "final";
  std::string model_equality_branch_policy = "first";
  bool euf_dynamic_ackermann_shadow = false;
  bool euf_diseq_ackermann = false;
  std::string pure_theory_filter = "auto";
  std::string theory_phase = "negative";
  std::string euf_prop_budget = "auto";
  std::string euf_prop_delivery = "1000";
  std::size_t nelson_oppen_arrangement_probe_limit = 0;
  std::size_t max_theory_conflicts = 0;
  std::size_t qf_uf_portfolio_conflicts = 2000;
  std::vector<std::string> cadical_options;
  primo::smt::CheckSatOptions solver_options;

  CLI::App app{"Primo SMT solver scaffold"};
  app.add_flag("-q,--quiet", quiet, "Suppress startup version banner");
  app.add_flag("--stats", print_stats, "Print solver statistics to stderr [default: off]");
  app.add_flag("--uf-symmetry-breaking", solver_options.uf_symmetry_breaking,
               "Enable default-on QF_UF symmetry breaking over invariant finite-domain values "
               "[default: on]");
  app.add_flag("--no-uf-symmetry-breaking", no_uf_symmetry_breaking,
               "Disable QF_UF finite-domain symmetry breaking [default: on]");
  app.add_flag(
      "--finite-domain-value-precedence", solver_options.finite_domain_value_precedence,
      "Enable fixed-term value precedence inside finite-domain symmetry breaking [default: on]");
  app.add_flag(
      "--no-finite-domain-value-precedence", no_finite_domain_value_precedence,
      "Disable fixed-term value precedence inside finite-domain symmetry breaking [default: on]");
  app.add_flag(
      "--finite-domain-equality-definitions", finite_domain_equality_definitions,
      "Enable guarded finite-domain equality definitions in auto preprocessing [default: on]");
  app.add_flag("--no-finite-domain-equality-definitions", no_finite_domain_equality_definitions,
               "Disable finite-domain SAT-visible equality definition clauses [default: on]");
  app.add_flag("--no-finite-domain-post-amo", no_finite_domain_post_amo,
               "Disable the post-simplification finite-domain AMO scan [default: on]");
  app.add_flag(
      "--eq-bridge", solver_options.eq_bridge,
      "Add equalities implied by every branch of selected QF_UF disjunctions [default: on]");
  app.add_flag(
      "--no-eq-bridge", no_eq_bridge,
      "Disable equality bridging unless required by the preprocessing preset [default: on]");
  app.add_flag("--lra-propagation", solver_options.lra_propagation,
               "Enable QF_LRA same-slack bound propagation (Milestone 3b) [default: on]");
  app.add_flag("--no-lra-propagation", no_lra_propagation,
               "Disable QF_LRA bound propagation (conflict detection only) [default: on]");
  app.add_flag(
      "--lra-row-propagation", solver_options.lra_row_propagation,
      "Enable experimental QF_LRA row-based/interval bound propagation across "
      "different variables (only takes effect if --lra-propagation is also on) [default: off]");
  app.add_flag("--lra-bidirectional-row-propagation",
               solver_options.lra_bidirectional_row_propagation,
               "Enable the reverse direction of --lra-row-propagation: tighten a row's "
               "other terms from its own bound, not just derive the row's own implied "
               "bound (only takes effect if --lra-row-propagation is also on) [default: off]");
  app.add_option("--lra-row-propagation-max-row-size",
                 solver_options.lra_row_propagation_limits.max_row_size,
                 "Cap on the row length --lra-row-propagation will derive an implied bound "
                 "for (default 64). Rows longer than this are skipped, not truncated")
      ->check(CLI::PositiveNumber);
  app.add_option("--lra-row-propagation-max-fanout",
                 solver_options.lra_row_propagation_limits.max_fanout,
                 "Cap on how many rows a single bound change may trigger a "
                 "--lra-row-propagation scan over (default 64). A variable occurring in more "
                 "rows than this propagates nothing that round")
      ->check(CLI::PositiveNumber);
  app.add_option("--lra-bidirectional-row-propagation-max-row-size",
                 solver_options.lra_row_propagation_limits.bidirectional_max_row_size,
                 "Row-length cap for the reverse direction "
                 "(--lra-bidirectional-row-propagation, default 32). Smaller than the forward "
                 "cap because that direction costs O(row size) per other term, not per row")
      ->check(CLI::PositiveNumber);
  app.add_option("--lra-bidirectional-row-propagation-max-fanout",
                 solver_options.lra_row_propagation_limits.bidirectional_max_fanout,
                 "Fanout cap for the reverse direction "
                 "(--lra-bidirectional-row-propagation, default 32)")
      ->check(CLI::PositiveNumber);
  app.add_flag("--lra-theory-decisions", solver_options.lra_theory_decisions,
               "Enable experimental QF_LRA theory-guided decisions: the arithmetic theory "
               "ranks candidate atoms via CaDiCaL's cb_decide and supplies the decision "
               "literal itself instead of leaving every decision to generic VSIDS [default: off]");
  app.add_flag("--lra-model-phase", solver_options.lra_model_phase,
               "Enable experimental QF_LRA model-based SAT phase hints: keep CaDiCaL's "
               "variable choice, but refresh arithmetic atom polarity from the current "
               "tableau assignment at quiescence points [default: off]");
  app.add_option("--lra-pivoting-rule", lra_pivoting_rule,
                 "QF_LRA Check/Pivot variable-selection rule: sparse (default -- prices "
                 "what a pivot costs: among the entering candidates that all repair the "
                 "violation equally, take the one occurring in the fewest rows, since "
                 "those are exactly the rows the pivot rewrites), bland "
                 "(anti-cycling-proven, the former default), dantzig "
                 "(largest-violation/largest-coefficient), or soi "
                 "(Sum-of-Infeasibilities Simplex, King/Barrett/Dutertre FMCAD 2013 -- a "
                 "global Check/Pivot objective across every violated row at once). All "
                 "but bland fall back to bland selection after enough pivots within one "
                 "check(), which is what keeps termination proven")
      ->check(CLI::IsMember({"bland", "dantzig", "soi", "sparse"}));
  app.add_flag("--lra-least-violated-leaving", lra_least_violated_leaving,
               "Experimental leaving-variable rule from Griggio's thesis (2.5.3, p.53):\n"
               "pick the LEAST-violated basic variable, tie-broken by the shorter row.\n"
               "Only meaningful under --lra-pivoting-rule sparse [default: off]");
  app.add_flag("--no-lra-sparse-leaving", no_lra_sparse_leaving,
               "Disable the default-on leaving-side half of --lra-pivoting-rule sparse "
               "(prefer repairing a violated variable whose row is short, using "
               "violation magnitude only as a tie-break), for ablation [default: on]");
  app.add_option("--lra-sparse-pricing-candidates",
                 solver_options.lra_tuning.sparse_pricing_candidates,
                 "How many eligible entering candidates --lra-pivoting-rule sparse scores "
                 "before taking the best seen so far (default 16; 0 means score every "
                 "eligible candidate). 1 is the cheapest possible pricing. Never cuts short "
                 "the search for whether an eligible candidate exists, so any value is sound")
      ->check(CLI::Range(0, 1000000));
  app.add_option("--lra-soi-minimize", lra_soi_minimize,
                 "How --lra-pivoting-rule soi shrinks a multi-row conflict before turning "
                 "it into a clause: quickxplain (default, Junker 2001), deletion "
                 "(iterative, and steerable by --lra-soi-minimize-order), or none "
                 "(ablation baseline). Inert under every other pivoting rule. Measured, "
                 "the error set averages 2.83 variables and never exceeded 9, so this "
                 "changes which minimal conflict is learned rather than how fast it is "
                 "found [default: quickxplain]");
  app.add_option("--lra-soi-minimize-order", lra_soi_minimize_order,
                 "Order --lra-soi-minimize deletion offers candidates for removal, worst "
                 "first: id (ascending, reproduces quickxplain's input order), "
                 "row-support (longest row first -- the row support dominates clause "
                 "length, so this sheds the rows contributing most literals), or "
                 "violation (largest bound violation first) [default: id]");
  app.add_option("--lra-bland-fallback-factor", solver_options.lra_tuning.bland_fallback_factor,
                 "Per-variable coefficient of check()'s Bland's-rule fallback point, which is "
                 "factor * variables + offset pivots within one check (default 4). Smaller "
                 "switches to Bland's rule sooner: slower but anti-cycling-proven")
      ->check(CLI::Range(0, 1000000));
  app.add_option("--lra-bland-fallback-offset", solver_options.lra_tuning.bland_fallback_offset,
                 "Constant term of the Bland's-rule fallback point (default 64)")
      ->check(CLI::Range(0, 1000000000));
  app.add_option("--mixed-dispatch", mixed_dispatch,
                 "Which formulas reach the mixed EUF/LRA solver: narrow (default -- only a "
                 "Bool-sorted function APPLICATION counts as an EUF predicate, so a formula "
                 "that is pure QF_LRA apart from propositional variables stays on the pure "
                 "path) or wide (a Bool-sorted constant counts too -- the pre-2026-08-07 "
                 "behaviour, routing any QF_LRA formula containing one propositional variable "
                 "to the mixed solver). Usually much worse, but genuinely better on some "
                 "instances; kept as a portfolio dimension and so differential fuzzing can "
                 "reach the mixed path [default: narrow]")
      ->check(CLI::IsMember({"narrow", "wide"}));
  app.add_flag("--lra-fixed-elimination", solver_options.lra_fixed_elimination,
               "Pivot a basic LRA variable out of the basis as soon as its bounds coincide "
               "(heuristics item H7). Off by default: it nets to nothing on the scrambled "
               "competition set, but doubles the >5MB band, so it is worth enabling on "
               "equality-dominated input [default: off]");
  app.add_flag("--no-qf-uflra-fixed-elimination", no_qf_uflra_fixed_elimination,
               "Disable default mixed QF_UFLRA fixed-variable elimination, which exposes "
               "EqRow interface equalities before the exact pair fallback [default: on]");
  app.add_flag("--ground-congruence", solver_options.ground_congruence,
               "Enable bounded SAT-visible ground congruence clauses [default: off]");
  app.add_flag(
      "--ground-unit-substitution", solver_options.ground_unit_substitution,
      "Enable experimental ground substitution from visible unit equalities [default: off]");
  app.add_flag("--guarded-ground-unit-substitution",
               solver_options.guarded_ground_unit_substitution,
               "Enable guarded ground substitution only when preprocessing metrics predict a win "
               "[default: off]");
  app.add_flag("--no-solved-constant-substitution", no_solved_constant_substitution,
               "Disable default QF_UF solved-constant substitution [default: on]");
  app.add_flag("--gaussian-elimination", solver_options.gaussian_elimination,
               "Enable default-on QF_LRA Gauss-Jordan elimination over asserted linear "
               "equality systems, feeding solve-eqs (campaign-fuzzed 2026-07-25) [default: on]");
  app.add_flag("--no-gaussian-elimination", no_gaussian_elimination,
               "Disable QF_LRA Gauss-Jordan equality-system elimination [default: on]");
  app.add_flag("--qf-uflra-candidate-final-scan", qf_uflra_candidate_final_scan,
               "Enable experimental mixed QF_UFLRA candidate-first final LRA equality scans before "
               "falling back to the full shared-Real pair scan [default: off]");
  app.add_option("--qf-uflra-lra-equality-producer", qf_uflra_lra_equality_producer,
                 "LRA-to-EUF opportunistic equality producer: legacy exact-offset row indexes "
                 "or model-unit current-model-filtered unit rows; adaptive bootstraps legacy to "
                 "a propagation fixed point, then uses model-unit until a shared-model split "
                 "[default: legacy]")
      ->check(CLI::IsMember({"legacy", "model-unit", "adaptive"}));
  app.add_flag("--qf-uflra-model-reconciliation", qf_uflra_model_reconciliation,
               "Explicitly enable convex QF_UFLRA shared-model reconciliation with scratch "
               "LRA mutation, EUF compatibility checks, and lazy positive equality splits "
               "(enabled by default)");
  app.add_flag("--no-qf-uflra-model-reconciliation", no_qf_uflra_model_reconciliation,
               "Disable default convex QF_UFLRA shared-model reconciliation and use the "
               "explained exact equality-sharing fallback");
  app.add_flag("--qf-uflra-online-euf", qf_uflra_online_euf,
               "Enable experimental incremental EUF callbacks and live final-model closure reuse "
               "in mixed QF_UFLRA solving [default: off]");
  app.add_flag("--monotone-elimination", monotone_elimination,
               "Enable experimental monotone (one-sided) real-variable elimination: a\n"
               "variable every constraint pushes the same way is discharged and its\n"
               "atoms folded to constants, shrinking the Boolean structure. Models are\n"
               "reconstructed for eliminated variables [default: off]");
  app.add_flag("--guarded-real-equality-lowering", guarded_real_equality_lowering,
               "Enable experimental guarded Real equality lowering: rewrite a disjunction "
               "with one Real equality child directly to two guarded inequalities, avoiding "
               "one Tseitin layer on some QF_LRA encodings [default: off]");
  app.add_flag("--arithmetic-atom-canonicalization",
               solver_options.arithmetic_atom_canonicalization,
               "Enable experimental primitive-integer canonicalization of proportional Real "
               "comparisons and equalities before clausification [default: off]");
  app.add_flag("--top-level-or-tseitin", top_level_or_tseitin,
               "Always encode top-level disjunctions with Tseitin support clauses [default: only "
               "when the automatic rule fires]");
  app.add_flag("--no-top-level-or-tseitin", no_top_level_or_tseitin,
               "Never encode them: keep each top-level disjunction as one clause, and turn "
               "the automatic rule off too [default: automatic rule enabled]");
  app.add_flag("--no-auto-top-level-or-tseitin", no_auto_top_level_or_tseitin,
               "Disable the automatic rule, which encodes Tseitin-style only on finite-domain "
               "problems having 1000-2500 top-level OR nodes and at most 700 negations [default: "
               "automatic rule enabled]");
  app.add_flag(
      "--preprocess-only", solver_options.preprocess_only,
      "Run parsing and preprocessing, print unknown, and skip SAT/theory solving [default: off]");
  CLI::Option* preprocessing_mode_option =
      app.add_option("--preprocessing-mode", preprocessing_mode,
                     "Preprocessing preset for all supported logics: off, basic, strong, or auto")
          ->check(CLI::IsMember({"off", "basic", "strong", "auto"}));
  CLI::Option* deprecated_preprocessing_mode_option =
      app.add_option("--qf-uf-preprocess", deprecated_qf_uf_preprocessing_mode,
                     "Deprecated alias for --preprocessing-mode")
          ->check(CLI::IsMember({"off", "basic", "strong", "auto"}));
  CLI::Option* auto_preprocess_selector_option =
      app.add_option("--auto-preprocess-selector", auto_preprocess_selector,
                     "Auto preprocessing selector: legacy assertion-count heuristics or "
                     "representation-stable structural analysis")
          ->check(CLI::IsMember({"legacy", "structural"}));
  CLI::Option* deprecated_auto_preprocess_selector_option =
      app.add_option("--qf-uf-auto-selector", deprecated_qf_uf_auto_selector,
                     "Deprecated alias for --auto-preprocess-selector")
          ->check(CLI::IsMember({"legacy", "structural"}));
  app.add_option("--auto-preprocess-large-conjunct-limit",
                 solver_options.auto_preprocess_tuning.large_conjunct_limit,
                 "Maximum implicit top-level conjunct occurrences for repeated auto "
                 "preprocessing; 0 means unlimited")
      ->check(CLI::Range(0, 100000000));
  app.add_option("--auto-basic-compact-term-limit",
                 solver_options.auto_preprocess_tuning.compact_term_limit,
                 "Maximum reachable terms for compact-clause basic auto preprocessing; 0 "
                 "means unlimited")
      ->check(CLI::Range(0, 100000000));
  app.add_option("--auto-basic-compact-min-conjuncts",
                 solver_options.auto_preprocess_tuning.compact_min_conjuncts,
                 "Minimum implicit top-level conjunct occurrences for compact-clause basic "
                 "auto preprocessing")
      ->check(CLI::Range(1, 100000000));
  app.add_option("--auto-basic-single-term-limit",
                 solver_options.auto_preprocess_tuning.finite_domain_term_limit,
                 "Maximum reachable terms for finite-domain basic auto preprocessing; 0 "
                 "means unlimited")
      ->check(CLI::Range(0, 100000000));
  app.add_option("--auto-basic-single-min-domains",
                 solver_options.auto_preprocess_tuning.finite_domain_min_domains,
                 "Minimum recognized finite domains for finite-domain basic auto preprocessing")
      ->check(CLI::Range(1, 100000000));
  app.add_option("--auto-basic-single-max-symmetry-terms",
                 solver_options.auto_preprocess_tuning.finite_domain_max_symmetry_terms,
                 "Maximum terms in the accepted finite-domain symmetry group; 0 means unlimited")
      ->check(CLI::Range(0, 100000000));
  app.add_flag("--no-auto-basic-single", no_auto_basic_single,
               "Disable the guarded auto basic-single preprocessing selector [default: on]");
  app.add_flag("--no-auto-basic-single-predicate-polarity", no_auto_basic_single_predicate_polarity,
               "Disable polarity phase for auto basic-single formulas with Boolean UF predicates "
               "[default: on]");
  app.add_flag(
      "--auto-basic-multi", auto_basic_multi,
      "Enable the experimental auto basic-multi preprocessing and phase heuristic [default: off]");
  app.add_flag("--no-auto-basic-multi", no_auto_basic_multi,
               "Disable the auto basic-multi preprocessing and phase heuristic [default: off]");
  app.add_flag("--no-auto-basic-multi-polarity", no_auto_basic_multi_polarity,
               "Disable polarity phase for auto basic-multi formulas [default: on]");
  CLI::Option* profile_option = app.add_option("--qf-uf-profile", qf_uf_profile,
                                               "QF_UF profile: default or tuned-interesting")
                                    ->check(CLI::IsMember({"default", "tuned-interesting"}));
  app.add_option("--qf-uf-portfolio", qf_uf_portfolio,
                 "QF_UF portfolio mode: off or basic-polarity")
      ->check(CLI::IsMember({"off", "basic-polarity"}));
  app.add_option("--qf-uf-portfolio-conflicts", qf_uf_portfolio_conflicts,
                 "Theory-conflict budget for the first QF_UF portfolio attempt")
      ->check(CLI::Range(1, 1000000000));
  app.add_option("--euf-prop", euf_prop,
                 "EUF propagation mode: auto, off, conflict, model, or online")
      ->check(CLI::IsMember({"auto", "off", "conflict", "model", "online"}));
  app.add_option("--euf-engine", euf_engine, "EUF engine: incremental or lazy")
      ->check(CLI::IsMember({"lazy", "incremental"}));
  app.add_option("--euf-explain-search", euf_explain_search,
                 "Incremental EUF explanation search: one-sided, bidirectional, proof-forest, "
                 "reoriented-proof-forest, or structural-proof-forest")
      ->check(CLI::IsMember({"one-sided", "bidirectional", "proof-forest",
                             "reoriented-proof-forest", "structural-proof-forest"}));
  app.add_flag("--euf-proof-forest-diagnostics", euf_proof_forest_diagnostics,
               "Record proof-forest path counters for incremental EUF explanations [default: off]");
  app.add_flag("--euf-diseq-prop-diagnostics", euf_diseq_propagation_diagnostics,
               "Record diagnostic-only QF_UF app-disequality propagation opportunity counters "
               "[default: off]");
  app.add_flag("--euf-ackermann-diagnostics", euf_ackermann_diagnostics,
               "Record diagnostic-only QF_UF Ackermannization cost counters [default: off]");
  app.add_flag("--nelson-oppen-diagnostics", nelson_oppen_diagnostics,
               "Record diagnostic-only Nelson-Oppen interface counters [default: off]");
  app.add_flag("--model-equality-branching", nelson_oppen_model_equality_branching,
               "Enable experimental QF_UFLRA model-equality branching over shared Real terms; "
               "not a full Nelson-Oppen model-equality implementation [default: off]");
  app.add_flag("--no-model-equality-mutation", no_nelson_oppen_model_equality_mutation,
               "Disable the Section 5.2 greedy LRA model-diversification sweep used by the "
               "experimental QF_UFLRA shared-model and model-equality branching paths "
               "[default: on when either path is enabled]");
  app.add_option("--model-equality-mutation-scope", model_equality_mutation_scope,
                 "QF_UFLRA LRA model-mutation scope: all shared Real terms or current EUF "
                 "care-pair endpoints [default: all]")
      ->check(CLI::IsMember({"all", "care"}));
  app.add_option("--nelson-oppen-prop", nelson_oppen_propagation,
                 "QF_UFLRA Nelson-Oppen interface equality propagation mode: exact, final, or "
                 "online. Exact keeps the mandatory convex closure but disables opportunistic "
                 "LRA producers; off is a backward-compatible alias for exact. Pure QF_UF and "
                 "QF_LRA keep their direct theory propagators [default: final]")
      ->check(CLI::IsMember({"off", "exact", "final", "online"}))
      ->default_val("final");
  app.add_option("--model-equality-branch-budget",
                 solver_options.nelson_oppen_model_equality_branch_budget,
                 "Maximum QF_UFLRA model-equality branch assumptions per check-sat")
      ->check(CLI::Range(0, 1000000));
  app.add_option("--model-equality-branch-policy", model_equality_branch_policy,
                 "QF_UFLRA model-equality branch policy: first, ranked, adaptive, or care-queued")
      ->check(CLI::IsMember({"first", "ranked", "adaptive", "care-queued"}));
  CLI::Option* no_arrangement_probe_limit_option =
      app.add_option("--nelson-oppen-arrangement-probe-limit", nelson_oppen_arrangement_probe_limit,
                     "Return unknown once mixed QF_UFLRA final equality sharing or "
                     "arrangement/model reconstruction has used this many temporary LRA "
                     "consistency probes")
          ->check(CLI::Range(1, 1000000000));
  app.add_flag(
      "--euf-dynamic-ackermann-shadow", euf_dynamic_ackermann_shadow,
      "Shadow-evaluate SAT-visible dynamic Ackermann clauses without adding them [default: off]");
  app.add_flag("--euf-diseq-ackermann", euf_diseq_ackermann,
               "Enable watched QF_UF disequality Ackermann propagation [default: off]");
  app.add_option("--euf-diseq-ackermann-max-width", solver_options.euf_diseq_ackermann_max_width,
                 "Maximum width of watched disequality Ackermann clauses")
      ->check(CLI::Range(2, 1000));
  app.add_option(
         "--euf-diseq-ackermann-queue-budget", solver_options.euf_diseq_ackermann_queue_budget,
         "Maximum watched disequality Ackermann candidates to inspect per propagation round")
      ->check(CLI::Range(0, 1000000000));
  app.add_option("--euf-diseq-ackermann-per-symbol", solver_options.euf_diseq_ackermann_per_symbol,
                 "Maximum watched disequality Ackermann candidates per function symbol")
      ->check(CLI::Range(0, 1000000000));
  app.add_option("--pure-theory-filter", pure_theory_filter,
                 "T-pure theory literal filtering mode: auto, on, or off")
      ->check(CLI::IsMember({"auto", "on", "off"}));
  CLI::Option* theory_phase_option =
      app.add_option(
             "--theory-phase", theory_phase,
             "Initial SAT phase for theory atoms: negative, positive, none, polarity, or auto")
          ->check(CLI::IsMember({"negative", "positive", "none", "polarity", "auto"}));
  app.add_option("--euf-prop-budget", euf_prop_budget,
                 "EUF propagation candidate budget: auto, unlimited, or a nonnegative integer");
  app.add_option("--euf-prop-delivery,--prop-delivery-budget", euf_prop_delivery,
                 "EUF propagation delivery budget: auto, unlimited, or a nonnegative integer; 0 "
                 "means unlimited");
  CLI::Option* max_theory_conflicts_option =
      app.add_option("--max-theory-conflicts", max_theory_conflicts,
                     "Return unknown after at least this many learned theory conflicts")
          ->check(CLI::Range(1, 1000000000));
  app.add_option_function<std::string>(
         "--cadical-option",
         [&cadical_options](const std::string& option) { cadical_options.push_back(option); },
         "Set a CaDiCaL integer option as name=value; may be repeated")
      ->multi_option_policy(CLI::MultiOptionPolicy::TakeAll);
  CLI::Option* preprocess_passes_option =
      app.add_option("--preprocessing-passes,--preprocess-passes",
                     solver_options.preprocessing_passes,
                     "Number of shared preprocessing passes to run")
          ->check(CLI::Range(0, 100));
  app.add_option("--boolean-flatten-threshold", solver_options.boolean_flatten_threshold,
                 "Flatten nested and/or nodes during preprocessing up to this arity; 0 disables")
      ->check(CLI::Range(0, 1000000));
  app.add_option("--boolean-flatten-post-threshold", solver_options.boolean_flatten_post_threshold,
                 "Apply the same bounded And/Or flattening again AFTER the rewriting "
                 "preprocessing passes, up to this arity; 0 disables. Those passes build "
                 "fresh nodes through TermManager, which no longer flattens at construction, "
                 "so they reintroduce nesting that --boolean-flatten-threshold (which runs "
                 "before them) cannot see. 0 disables [default: 2048]")
      ->check(CLI::Range(0, 1000000));
  app.add_option("--boolean-implies-rewrite", boolean_implies_rewrite,
                 "Post-preprocessing implication rewrite: off, clause (only expose flat "
                 "clauses), or all (rewrite A=>B to (or (not A) B) without distribution)")
      ->check(CLI::IsMember({"off", "clause", "all"}));
  app.add_option("--preprocessing-adaptive-extra-passes",
                 solver_options.preprocessing_adaptive_extra_passes,
                 "Additional adaptive preprocessing passes after the configured pass cap")
      ->check(CLI::Range(0, 100));
  app.add_option("--preprocessing-adaptive-shrink-percent",
                 solver_options.preprocessing_adaptive_shrink_percent,
                 "Minimum reachable-term shrink percentage required for another adaptive pass")
      ->check(CLI::Range(0, 100));
  app.add_option(
         "--finite-domain-eqdefs-auto-domain-limit",
         solver_options.finite_domain_eqdefs_auto_domain_limit,
         "Maximum finite-domain value count for auto equality definitions; 0 means unlimited")
      ->check(CLI::Range(0, 1000000));
  app.add_option("--finite-domain-eqdefs-auto-clause-limit",
                 solver_options.finite_domain_eqdefs_auto_clause_limit,
                 "Maximum generated equality-definition clauses per check-sat in auto; 0 "
                 "means unlimited")
      ->check(CLI::Range(0, 100000000));
  app.add_option("--finite-domain-eqdefs-auto-term-limit",
                 solver_options.finite_domain_eqdefs_auto_term_limit,
                 "Maximum initial reachable-term count for auto equality definitions on "
                 "single-assertion formulas; 0 means unlimited")
      ->check(CLI::Range(0, 100000000));
  CLI::Option* prop_interval_option =
      app.add_option("--prop-interval", solver_options.euf_propagation_interval,
                     "Online EUF propagation discovery interval")
          ->check(CLI::Range(1, 1000000));
  CLI::Option* prop_assign_threshold_option =
      app.add_option("--prop-assign-threshold", solver_options.euf_propagation_assign_threshold,
                     "Skip online EUF propagation discovery above this assigned-atom fraction")
          ->check(CLI::Range(0.0, 1.0));
  app.add_option("files", files, "SMT-LIB input files. Reads stdin when omitted")
      ->type_name("FILE");
  app.set_version_flag("--version", version_text());

  // Sort the 60-odd options into sections for --help. Done here, by name,
  // rather than by threading a group into every add_flag/add_option call:
  // one table to read instead of sixty scattered ->group() calls, and a new
  // option lands in the right section by virtue of being named consistently
  // with the ones it belongs beside.
  //
  // The split follows what each option acts *on*, which its own help text
  // already states: theory solver (QF_LRA / QF_UF), preprocessing, the SAT
  // layer and DPLL(T) boundary, or the run itself. Only the placements that
  // are not obvious from the prefix are listed explicitly below; anything
  // left unmatched lands in "Other options", which is a bug rather than a
  // category -- `primo --help` should never print that heading.
  const auto starts_with = [](const std::string& text, std::string_view prefix) {
    return text.rfind(prefix, 0) == 0;
  };
  const auto group_for = [&starts_with](const std::string& name) -> std::string {
    if (starts_with(name, "--lra-") || starts_with(name, "--no-lra-") ||
        name == "--gaussian-elimination" || name == "--no-gaussian-elimination" ||
        name == "--qf-uflra-candidate-final-scan" || name == "--qf-uflra-lra-equality-producer" ||
        name == "--qf-uflra-model-reconciliation" || name == "--no-qf-uflra-model-reconciliation" ||
        name == "--no-qf-uflra-fixed-elimination" || name == "--guarded-real-equality-lowering" ||
        name == "--monotone-elimination" || name == "--lra-least-violated-leaving" ||
        name == "--mixed-dispatch") {
      return kGroupLra;
    }
    if (name == "--preprocessing-mode" || name == "--qf-uf-preprocess" ||
        name == "--arithmetic-atom-canonicalization" || name == "--auto-preprocess-selector" ||
        name == "--qf-uf-auto-selector" || name == "--boolean-flatten-threshold" ||
        name == "--boolean-flatten-post-threshold" || name == "--boolean-implies-rewrite" ||
        starts_with(name, "--auto-preprocess-") || starts_with(name, "--preprocessing-") ||
        starts_with(name, "--preprocess-passes")) {
      return kGroupPreprocessing;
    }
    // The SAT layer proper, plus the knobs that sit on the DPLL(T)
    // boundary: initial phase for theory atoms, T-pure filtering, the
    // theory-conflict budget, and how top-level disjunctions get clausified.
    if (name == "--cadical-option" || name == "--max-theory-conflicts" ||
        name == "--qf-uflra-online-euf" || name == "--pure-theory-filter" ||
        name == "--theory-phase" || starts_with(name, "--top-level-or-tseitin") ||
        starts_with(name, "--no-top-level-or-tseitin") ||
        starts_with(name, "--no-auto-top-level-or-tseitin")) {
      return kGroupSat;
    }
    // How the run behaves rather than how the solver reasons. `--help` is
    // CLI11's own built-in flag and would otherwise be the one option left
    // unclassified.
    if (name == "-q" || name == "--quiet" || name == "--stats" || name == "--version" ||
        name == "--help" || name == "--preprocess-only" || name == "files") {
      return kGroupProcedural;
    }
    // Everything else is QF_UF/EUF: the euf-*, finite-domain-*, ground-*,
    // qf-uf-* and auto-basic-* families, plus eq-bridge, symmetry breaking,
    // solved-constant substitution, and the online-propagation knobs.
    if (starts_with(name, "--euf-") || starts_with(name, "--no-euf-") ||
        starts_with(name, "--finite-domain-") || starts_with(name, "--no-finite-domain-") ||
        starts_with(name, "--ground-") || starts_with(name, "--guarded-ground-") ||
        starts_with(name, "--model-equality-") || starts_with(name, "--no-model-equality-") ||
        starts_with(name, "--nelson-oppen-") || starts_with(name, "--qf-uf-") ||
        starts_with(name, "--auto-basic-") || starts_with(name, "--no-auto-basic-") ||
        name == "--eq-bridge" || name == "--no-eq-bridge" || name == "--uf-symmetry-breaking" ||
        name == "--no-uf-symmetry-breaking" || name == "--no-solved-constant-substitution" ||
        name == "--prop-interval" || name == "--prop-assign-threshold" ||
        name == "--prop-delivery-budget") {
      return kGroupUf;
    }
    return "Other options";
  };
  for (CLI::Option* option : app.get_options()) {
    // get_name() gives the canonical (longest) name; aliases follow the
    // same family, so classifying on it is enough.
    std::string group = group_for(option->get_name());
    if (group == "Other options") {
      // Self-policing: whoever adds an option the table above does not
      // cover finds out immediately, rather than by noticing a stray
      // heading in --help months later. Costs one string compare per
      // option, once per process.
      std::cerr << "warning: option " << option->get_name()
                << " has no --help section; add it to group_for() in main.cpp\n";
    }
    option->group(std::move(group));
  }

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& error) {
    return app.exit(error);
  }

  if (qf_uflra_online_euf && nelson_oppen_model_equality_branching) {
    std::cerr << "error: --qf-uflra-online-euf and --model-equality-branching cannot be used "
                 "together (untested interaction between online interface-equality "
                 "backtracking and shared-equality-decision state)\n";
    return 2;
  }
  if (deprecated_preprocessing_mode_option->count() > 0) {
    if (preprocessing_mode_option->count() > 0) {
      std::cerr << "error: --preprocessing-mode and deprecated --qf-uf-preprocess cannot be used "
                   "together\n";
      return 2;
    }
    preprocessing_mode = deprecated_qf_uf_preprocessing_mode;
    std::cerr << "warning: --qf-uf-preprocess is deprecated; use --preprocessing-mode\n";
  }
  if (deprecated_auto_preprocess_selector_option->count() > 0) {
    if (auto_preprocess_selector_option->count() > 0) {
      std::cerr << "error: --auto-preprocess-selector and deprecated --qf-uf-auto-selector cannot "
                   "be used together\n";
      return 2;
    }
    auto_preprocess_selector = deprecated_qf_uf_auto_selector;
    std::cerr << "warning: --qf-uf-auto-selector is deprecated; use "
                 "--auto-preprocess-selector\n";
  }

  if (preprocessing_mode == "off") {
    solver_options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  } else if (preprocessing_mode == "strong") {
    solver_options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  } else if (preprocessing_mode == "auto") {
    solver_options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  } else {
    solver_options.preprocessing_mode = primo::smt::PreprocessingMode::Basic;
  }
  solver_options.auto_preprocess_selector = auto_preprocess_selector == "structural"
                                                ? primo::smt::AutoPreprocessSelectorMode::Structural
                                                : primo::smt::AutoPreprocessSelectorMode::Legacy;
  if (boolean_implies_rewrite == "clause") {
    solver_options.boolean_implies_rewrite = primo::smt::BooleanImpliesRewriteMode::Clause;
  } else if (boolean_implies_rewrite == "all") {
    solver_options.boolean_implies_rewrite = primo::smt::BooleanImpliesRewriteMode::All;
  } else {
    solver_options.boolean_implies_rewrite = primo::smt::BooleanImpliesRewriteMode::Off;
  }
  if (profile_option->count() > 0 && qf_uf_profile == "tuned-interesting") {
    solver_options.uf_symmetry_breaking = true;
    solver_options.eq_bridge = true;
    if (preprocess_passes_option->count() == 0) {
      solver_options.preprocessing_passes = 4;
    }
    if (prop_interval_option->count() == 0) {
      solver_options.euf_propagation_interval = 32;
    }
    if (prop_assign_threshold_option->count() == 0) {
      solver_options.euf_propagation_assign_threshold = 0.25;
    }
    if (euf_prop_delivery == "auto") {
      euf_prop_delivery = "1000";
    }
  }
  if (qf_uf_portfolio == "basic-polarity") {
    solver_options.qf_uf_portfolio = primo::smt::QfUfPortfolioMode::BasicPolarity;
  } else {
    solver_options.qf_uf_portfolio = primo::smt::QfUfPortfolioMode::Off;
  }
  solver_options.qf_uf_portfolio_conflicts = qf_uf_portfolio_conflicts;
  if (no_uf_symmetry_breaking) {
    solver_options.uf_symmetry_breaking = false;
  }
  if (no_finite_domain_value_precedence) {
    solver_options.finite_domain_value_precedence = false;
  }
  if (finite_domain_equality_definitions) {
    solver_options.auto_finite_domain_equality_definitions = true;
    solver_options.finite_domain_equality_definitions = true;
  }
  if (no_finite_domain_equality_definitions) {
    solver_options.finite_domain_equality_definitions = false;
    solver_options.auto_finite_domain_equality_definitions = false;
  }
  if (no_finite_domain_post_amo) {
    solver_options.finite_domain_post_amo = false;
  }
  if (no_eq_bridge) {
    solver_options.eq_bridge = false;
  }
  if (no_lra_propagation) {
    solver_options.lra_propagation = false;
  }
  if (no_solved_constant_substitution) {
    solver_options.solved_constant_substitution = false;
  }
  if (no_gaussian_elimination) {
    solver_options.gaussian_elimination = false;
  }
  if (no_qf_uflra_fixed_elimination) {
    solver_options.qf_uflra_fixed_elimination = false;
  }
  solver_options.qf_uflra_candidate_final_scan = qf_uflra_candidate_final_scan;
  if (qf_uflra_lra_equality_producer == "model-unit") {
    solver_options.qf_uflra_lra_equality_producer =
        primo::smt::QfUflraLraEqualityProducer::ModelUnit;
  } else if (qf_uflra_lra_equality_producer == "adaptive") {
    solver_options.qf_uflra_lra_equality_producer =
        primo::smt::QfUflraLraEqualityProducer::Adaptive;
  } else {
    solver_options.qf_uflra_lra_equality_producer = primo::smt::QfUflraLraEqualityProducer::Legacy;
  }
  if (qf_uflra_model_reconciliation) {
    solver_options.qf_uflra_model_reconciliation = true;
  }
  if (no_qf_uflra_model_reconciliation) {
    solver_options.qf_uflra_model_reconciliation = false;
  }
  solver_options.qf_uflra_online_euf = qf_uflra_online_euf;
  if (monotone_elimination) {
    solver_options.monotone_elimination = true;
  }
  if (guarded_real_equality_lowering) {
    solver_options.guarded_real_equality_lowering = true;
  }
  if (top_level_or_tseitin) {
    solver_options.top_level_or_tseitin = true;
  }
  if (no_top_level_or_tseitin) {
    solver_options.top_level_or_tseitin = false;
    solver_options.auto_top_level_or_tseitin = false;
  }
  if (no_auto_top_level_or_tseitin) {
    solver_options.auto_top_level_or_tseitin = false;
  }
  if (no_auto_basic_single) {
    solver_options.auto_basic_single_preprocessing = false;
    solver_options.auto_basic_single_predicate_polarity = false;
  }
  if (no_auto_basic_single_predicate_polarity) {
    solver_options.auto_basic_single_predicate_polarity = false;
  }
  if (auto_basic_multi) {
    solver_options.auto_basic_multi_preprocessing = true;
    solver_options.auto_basic_multi_polarity = true;
  }
  if (no_auto_basic_multi) {
    solver_options.auto_basic_multi_preprocessing = false;
    solver_options.auto_basic_multi_polarity = false;
  }
  if (no_auto_basic_multi_polarity) {
    solver_options.auto_basic_multi_polarity = false;
  }
  if (euf_prop == "auto") {
    solver_options.euf_propagation = primo::smt::EufPropagationMode::Auto;
  } else if (euf_prop == "conflict") {
    solver_options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  } else if (euf_prop == "model") {
    solver_options.euf_propagation = primo::smt::EufPropagationMode::Model;
  } else if (euf_prop == "online") {
    solver_options.euf_propagation = primo::smt::EufPropagationMode::Online;
  } else {
    solver_options.euf_propagation = primo::smt::EufPropagationMode::Off;
  }
  if (euf_engine == "incremental") {
    solver_options.euf_engine = primo::smt::EufEngine::Incremental;
  } else {
    solver_options.euf_engine = primo::smt::EufEngine::Lazy;
  }
  if (euf_explain_search == "bidirectional") {
    solver_options.euf_explain_search = primo::smt::EufExplainSearchMode::BidirectionalBfs;
  } else if (euf_explain_search == "proof-forest") {
    solver_options.euf_explain_search = primo::smt::EufExplainSearchMode::ProofForest;
  } else if (euf_explain_search == "reoriented-proof-forest") {
    solver_options.euf_explain_search = primo::smt::EufExplainSearchMode::ReorientedProofForest;
  } else if (euf_explain_search == "structural-proof-forest") {
    solver_options.euf_explain_search = primo::smt::EufExplainSearchMode::StructuralProofForest;
  } else {
    solver_options.euf_explain_search = primo::smt::EufExplainSearchMode::OneSidedBfs;
  }
  solver_options.euf_proof_forest_diagnostics = euf_proof_forest_diagnostics;
  if (lra_least_violated_leaving) {
    solver_options.lra_least_violated_leaving = true;
  }
  if (no_lra_sparse_leaving) {
    solver_options.lra_sparse_leaving = false;
  }
  if (mixed_dispatch == "wide") {
    solver_options.mixed_dispatch = primo::smt::MixedDispatchMode::Wide;
  }
  if (lra_soi_minimize == "deletion") {
    solver_options.lra_tuning.soi_minimization = primo::smt::LraSoiMinimization::Deletion;
  } else if (lra_soi_minimize == "none") {
    solver_options.lra_tuning.soi_minimization = primo::smt::LraSoiMinimization::None;
  } else {
    solver_options.lra_tuning.soi_minimization = primo::smt::LraSoiMinimization::QuickXplain;
  }
  if (lra_soi_minimize_order == "row-support") {
    solver_options.lra_tuning.soi_minimization_order =
        primo::smt::LraSoiMinimizationOrder::RowSupport;
  } else if (lra_soi_minimize_order == "violation") {
    solver_options.lra_tuning.soi_minimization_order =
        primo::smt::LraSoiMinimizationOrder::Violation;
  } else {
    solver_options.lra_tuning.soi_minimization_order = primo::smt::LraSoiMinimizationOrder::Id;
  }
  if (lra_pivoting_rule == "dantzig") {
    solver_options.lra_pivoting_rule = primo::smt::LraPivotingRule::Dantzig;
  } else if (lra_pivoting_rule == "soi") {
    solver_options.lra_pivoting_rule = primo::smt::LraPivotingRule::Soi;
  } else if (lra_pivoting_rule == "sparse") {
    solver_options.lra_pivoting_rule = primo::smt::LraPivotingRule::Sparse;
  } else {
    solver_options.lra_pivoting_rule = primo::smt::LraPivotingRule::Bland;
  }
  solver_options.euf_diseq_propagation_diagnostics = euf_diseq_propagation_diagnostics;
  solver_options.euf_ackermann_diagnostics = euf_ackermann_diagnostics;
  solver_options.nelson_oppen_diagnostics = nelson_oppen_diagnostics;
  solver_options.nelson_oppen_model_equality_branching = nelson_oppen_model_equality_branching;
  if (nelson_oppen_propagation == "off" || nelson_oppen_propagation == "exact") {
    solver_options.nelson_oppen_propagation = primo::smt::NelsonOppenPropagationMode::Exact;
  } else if (nelson_oppen_propagation == "online") {
    solver_options.nelson_oppen_propagation = primo::smt::NelsonOppenPropagationMode::Online;
  } else {
    solver_options.nelson_oppen_propagation = primo::smt::NelsonOppenPropagationMode::Final;
  }
  if (no_nelson_oppen_model_equality_mutation) {
    solver_options.nelson_oppen_model_equality_mutation = false;
  }
  solver_options.nelson_oppen_model_equality_mutation_scope =
      model_equality_mutation_scope == "care" ? primo::smt::ModelEqualityMutationScope::Care
                                              : primo::smt::ModelEqualityMutationScope::All;
  if (model_equality_branch_policy == "adaptive") {
    solver_options.nelson_oppen_model_equality_branch_policy =
        primo::smt::ModelEqualityBranchPolicy::Adaptive;
  } else if (model_equality_branch_policy == "care-queued") {
    solver_options.nelson_oppen_model_equality_branch_policy =
        primo::smt::ModelEqualityBranchPolicy::CareQueued;
  } else if (model_equality_branch_policy == "ranked") {
    solver_options.nelson_oppen_model_equality_branch_policy =
        primo::smt::ModelEqualityBranchPolicy::Ranked;
  } else {
    solver_options.nelson_oppen_model_equality_branch_policy =
        primo::smt::ModelEqualityBranchPolicy::First;
  }
  solver_options.euf_dynamic_ackermann_shadow = euf_dynamic_ackermann_shadow;
  solver_options.euf_diseq_ackermann = euf_diseq_ackermann;
  if (pure_theory_filter == "off") {
    solver_options.pure_theory_filter = primo::smt::PureTheoryFilterMode::Off;
  } else if (pure_theory_filter == "on") {
    solver_options.pure_theory_filter = primo::smt::PureTheoryFilterMode::On;
  } else {
    solver_options.pure_theory_filter = primo::smt::PureTheoryFilterMode::Auto;
  }
  if (theory_phase == "positive") {
    solver_options.theory_phase = primo::smt::TheoryPhaseMode::Positive;
  } else if (theory_phase == "none") {
    solver_options.theory_phase = primo::smt::TheoryPhaseMode::None;
  } else if (theory_phase == "polarity") {
    solver_options.theory_phase = primo::smt::TheoryPhaseMode::Polarity;
  } else if (theory_phase == "auto") {
    solver_options.theory_phase = primo::smt::TheoryPhaseMode::Auto;
  } else {
    solver_options.theory_phase = primo::smt::TheoryPhaseMode::Negative;
  }
  if (theory_phase_option->count() > 0) {
    solver_options.auto_basic_single_predicate_polarity = false;
    solver_options.auto_basic_multi_polarity = false;
  }
  const std::optional<primo::smt::AutoBudget> parsed_prop_budget =
      parse_auto_budget(euf_prop_budget);
  const std::optional<primo::smt::AutoBudget> parsed_prop_delivery =
      parse_auto_budget(euf_prop_delivery);
  if (!parsed_prop_budget.has_value()) {
    std::cerr << "error: invalid --euf-prop-budget value: " << euf_prop_budget << "\n";
    return 1;
  }
  if (!parsed_prop_delivery.has_value()) {
    std::cerr << "error: invalid --euf-prop-delivery value: " << euf_prop_delivery << "\n";
    return 1;
  }
  solver_options.euf_propagation_budget = parsed_prop_budget.value();
  solver_options.euf_propagation_delivery = parsed_prop_delivery.value();
  if (max_theory_conflicts_option->count() > 0) {
    solver_options.max_theory_conflicts = max_theory_conflicts;
  }
  if (no_arrangement_probe_limit_option->count() > 0) {
    solver_options.nelson_oppen_arrangement_probe_limit = nelson_oppen_arrangement_probe_limit;
  }
  for (const std::string& option : cadical_options) {
    const std::optional<std::pair<std::string, int>> parsed = parse_cadical_option(option);
    if (!parsed.has_value()) {
      std::cerr << "error: invalid --cadical-option value: " << option << "\n";
      return 1;
    }
    solver_options.cadical_options.push_back(parsed.value());
  }

  if (!quiet) {
    print_startup_banner();
  }
  g_print_timeout_statistics = print_stats;
  if (print_stats) {
    solver_options.live_statistics = &g_live_statistics;
  }

  if (files.empty()) {
    std::ostringstream contents;
    contents << std::cin.rdbuf();
    const primo::smt::CheckSatOutcome outcome =
        primo::smt::check_sat_smtlib(contents.str(), solver_options);
    print_outcome(outcome, print_stats);
    if (!outcome.message.empty()) {
      std::cerr << outcome.message << "\n";
    }
    return outcome.result == primo::smt::CheckSatResult::Unsupported ? 2 : 0;
  }

  int exit_code = 0;
  for (const auto& file : files) {
    if (!can_read_file(file)) {
      std::cerr << "error: cannot read input file: " << file << "\n";
      return 1;
    }
    const primo::smt::CheckSatOutcome outcome =
        primo::smt::check_sat_smtlib(read_file(file), solver_options);
    print_outcome(outcome, print_stats);
    if (!outcome.message.empty()) {
      std::cerr << file << ": " << outcome.message << "\n";
    }
    if (outcome.result == primo::smt::CheckSatResult::Unsupported) {
      exit_code = 2;
    }
  }
  return exit_code;
}
