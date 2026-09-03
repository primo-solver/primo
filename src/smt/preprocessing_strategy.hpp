#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/ids.hpp"
#include "smt/check_sat.hpp"
#include "smt/preprocessing.hpp"
#include "smt/strategy.hpp"
#include "smt/tactic.hpp"

namespace primo::smt {

class Frontend;

enum class PreprocessingProfileKind {
  Off,
  Basic,
  FiniteDomainBasic,
  CompactClauseBasic,
  Strong,
  SingleAssertion,
  LargeInputBypass,
  QfLia,
};

// Immutable result of production profile selection. It contains decisions and
// analysis evidence only; executing the selected transformations is the job of
// make_preprocessing_strategy().
struct PreprocessingProfile {
  PreprocessingProfileKind kind = PreprocessingProfileKind::Off;
  std::size_t initial_reachable_terms = 0;
  bool structural_selector = false;
  bool preprocessing_loop_enabled = false;
  bool auto_large_assertion_skip = false;
  bool auto_basic_single = false;
  bool auto_basic_multi = false;
  bool auto_single_assertion = false;
  bool auto_strong = false;
  bool strong_preprocessing = false;
  bool use_eq_bridge = false;
  bool use_symmetry_breaking = false;
  bool use_finite_domain_amo = false;
  bool use_finite_domain_equality_definitions = false;
  bool use_bounded_auto_equality_definitions = false;
  bool use_ground_congruence_definitions = false;
  bool use_ground_unit_substitution = false;
  bool use_guarded_ground_unit_substitution = false;
  std::size_t preprocessing_passes = 0;
  std::size_t adaptive_shrink_percent = 0;
  ImplicitConjunctionAnalysis conjunction_analysis;
  bool selector_shadow_evaluated = false;
  bool selector_legacy_disagreement = false;
  FiniteDomainAnalysis finite_domain_analysis;
  std::shared_ptr<const FiniteDomainAtMostOneResult> raw_finite_domain =
      std::make_shared<const FiniteDomainAtMostOneResult>();
  std::optional<UfSymmetryBreakingResult> analyzed_symmetry;

  std::vector<StrategyParameter> strategy_parameters() const;
};

struct PreparedPreprocessingStrategy {
  Strategy strategy;
  AnyOf result;
  PreprocessingProfile profile;
};

PreprocessingProfile analyze_preprocessing_profile(core::TermManager& terms,
                                                   const std::vector<core::TermId>& assertions,
                                                   const CheckSatOptions& options, bool is_qf_lia);

// Strategy construction is deliberately free of live session state: the
// returned value reads its term manager and statistics from the TacticContext
// it is executed under, so it stays valid after the session that built it is
// gone. The frontend is still borrowed for the leaves that create fresh
// symbols.
Strategy make_preprocessing_strategy(Frontend& frontend, const CheckSatOptions& options,
                                     const PreprocessingProfile& profile,
                                     const std::vector<core::TermId>& symmetry_source_assertions);

// Executes initial normalization, analyzes the transformed goal, builds the
// immutable production strategy, and executes its remaining nodes. QF_LIA's
// audited preprocessing restriction is reflected back into options for solver
// dispatch, exactly as in the historical session pipeline.
PreparedPreprocessingStrategy prepare_and_execute_preprocessing_strategy(
    Frontend& frontend, std::vector<core::TermId> assertions, CheckSatOptions& options,
    TacticContext& context);

}  // namespace primo::smt
