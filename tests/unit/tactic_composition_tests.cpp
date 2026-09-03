#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "core/ids.hpp"
#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/frontend.hpp"
#include "smt/preprocessing_strategy.hpp"
#include "smt/solver_leaves.hpp"
#include "smt/strategy.hpp"
#include "smt/tactic.hpp"
#include "util/statistics.hpp"

namespace {

void check(bool condition) {
  if (!condition) {
    std::abort();
  }
}

enum LogicMask : unsigned {
  kQfUf = 1U << 0U,
  kQfLra = 1U << 1U,
  kQfUflra = 1U << 2U,
  kQfLia = 1U << 3U,
  kAllLogics = kQfUf | kQfLra | kQfUflra | kQfLia,
};

struct CompositionProfile {
  std::string_view name;
  primo::smt::CheckSatOptions options;
  unsigned logics;
  std::string_view preprocessing_fingerprint;
  std::vector<std::string_view> required_canonical_fragments;
  std::vector<std::string_view> forbidden_canonical_fragments;
  std::vector<std::string_view> required_statistics;
};

primo::smt::CheckSatOptions off_options() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Off;
  return options;
}

primo::smt::CheckSatOptions basic_options() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Basic;
  options.preprocessing_passes = 1;
  options.preprocessing_adaptive_extra_passes = 0;
  return options;
}

primo::smt::CheckSatOptions strong_options() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Strong;
  options.preprocessing_passes = 4;
  options.preprocessing_adaptive_extra_passes = 0;
  return options;
}

primo::smt::CheckSatOptions adaptive_options() {
  primo::smt::CheckSatOptions options;
  options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  options.preprocessing_passes = 1;
  options.preprocessing_adaptive_extra_passes = 2;
  options.preprocessing_adaptive_shrink_percent = 10;
  return options;
}

primo::smt::CheckSatOptions ground_substitution_options(bool guarded) {
  primo::smt::CheckSatOptions options = basic_options();
  options.eq_bridge = true;
  options.ground_unit_substitution = !guarded;
  options.guarded_ground_unit_substitution = guarded;
  return options;
}

primo::smt::CheckSatOptions no_gaussian_options() {
  primo::smt::CheckSatOptions options = strong_options();
  options.gaussian_elimination = false;
  return options;
}

primo::smt::CheckSatOptions monotone_options() {
  primo::smt::CheckSatOptions options = strong_options();
  options.monotone_elimination = true;
  return options;
}

primo::smt::CheckSatOptions split_options(std::size_t count, bool strong) {
  primo::smt::CheckSatOptions options = strong ? strong_options() : primo::smt::CheckSatOptions{};
  options.experimental_split_bools = count;
  return options;
}

std::vector<CompositionProfile> profiles() {
  return {
      {"default",
       {},
       kAllLogics,
       "fnv1a64:76beb1c5a9bc1669",
       {"preprocessing-profile", "normalize-boolean"},
       {},
       {}},
      {"off",
       off_options(),
       kAllLogics,
       "fnv1a64:888d54e6a5855cc8",
       {"preprocessing-profile", "finite-domain-preprocess", "purify-mixed"},
       {"adaptive-reachable-term-shrink"},
       {}},
      {"basic",
       basic_options(),
       kAllLogics,
       "fnv1a64:0647c0d62293d019",
       {"(repeat 1 \"adaptive-reachable-term-shrink\"", "preprocessing-round"},
       {},
       {":tactic.preprocessing-round.invocations"}},
      {"strong",
       strong_options(),
       kAllLogics,
       "fnv1a64:e63763fe380476ee",
       {"(repeat 4 \"adaptive-reachable-term-shrink\"", "finite-domain-preprocess"},
       {},
       {":tactic.preprocessing-round.invocations"}},
      {"adaptive",
       adaptive_options(),
       kAllLogics,
       "fnv1a64:2d0816ed8ccbd227",
       {"(repeat 3 \"adaptive-reachable-term-shrink\"", "preprocessing-round"},
       {},
       {":tactic.preprocessing-round.invocations"}},
      {"ground-substitution",
       ground_substitution_options(false),
       kQfUf | kQfUflra,
       "fnv1a64:bf05cf4ab203a78c",
       {"\"ground-unit-substitution\" \"true\"", "\"guarded-ground-unit-substitution\" \"false\""},
       {},
       {":tactic.ground-unit-substitution.invocations"}},
      {"guarded-substitution",
       ground_substitution_options(true),
       kQfUf | kQfUflra,
       "fnv1a64:6d378fe36c15ddc4",
       {"\"ground-unit-substitution\" \"false\"", "\"guarded-ground-unit-substitution\" \"true\""},
       {},
       {":tactic.ground-unit-substitution.invocations"}},
      {"no-gaussian",
       no_gaussian_options(),
       kQfLra | kQfUflra,
       "fnv1a64:eedc00a1afe6ec2b",
       {"\"gaussian-elimination\" \"false\""},
       {},
       {":tactic.solve-eqs.invocations"}},
      {"monotone",
       monotone_options(),
       kQfLra | kQfUflra,
       "fnv1a64:810ed57b0e9bf989",
       {"eliminate-monotone"},
       {},
       {":tactic.eliminate-monotone.invocations"}},
      {"split-1",
       split_options(1, false),
       kQfUf | kQfUflra,
       "fnv1a64:76beb1c5a9bc1669",
       {},
       {},
       {}},
      {"strong-split-2",
       split_options(2, true),
       kQfUf | kQfUflra,
       "fnv1a64:e63763fe380476ee",
       {},
       {},
       {}},
  };
}

struct FormulaCase {
  unsigned logic;
  primo::smt::CheckSatResult expected;
  std::string_view input;
};

std::vector<FormulaCase> formula_cases() {
  using primo::smt::CheckSatResult;
  return {
      {kQfUf, CheckSatResult::Sat, R"(
        (set-logic QF_UF)
        (declare-fun p () Bool)
        (declare-fun q () Bool)
        (assert (or p q))
        (check-sat)
      )"},
      {kQfUf, CheckSatResult::Unsat, R"(
        (set-logic QF_UF)
        (declare-sort U 0)
        (declare-fun a () U)
        (declare-fun b () U)
        (declare-fun f (U) Bool)
        (assert (= a b))
        (assert (f a))
        (assert (not (f b)))
        (check-sat)
      )"},
      {kQfLra, CheckSatResult::Sat, R"(
        (set-logic QF_LRA)
        (declare-fun x () Real)
        (declare-fun y () Real)
        (assert (= y (+ x 1)))
        (assert (<= y 2))
        (check-sat)
      )"},
      {kQfLra, CheckSatResult::Unsat, R"(
        (set-logic QF_LRA)
        (declare-fun x () Real)
        (assert (< x 0))
        (assert (>= x 0))
        (check-sat)
      )"},
      {kQfUflra, CheckSatResult::Sat, R"(
        (set-logic QF_UFLRA)
        (declare-fun x () Real)
        (declare-fun p () Bool)
        (declare-fun f (Real) Real)
        (assert (or p (not p)))
        (assert (= (f x) x))
        (assert (<= x 1))
        (check-sat)
      )"},
      {kQfUflra, CheckSatResult::Unsat, R"(
        (set-logic QF_UFLRA)
        (declare-fun x () Real)
        (declare-fun y () Real)
        (declare-fun f (Real) Real)
        (assert (= x y))
        (assert (< (f x) 0))
        (assert (>= (f y) 0))
        (check-sat)
      )"},
      {kQfLia, CheckSatResult::Sat, R"(
        (set-logic QF_LIA)
        (declare-fun x () Int)
        (declare-fun p () Bool)
        (assert (= x 2))
        (assert (or p (not p)))
        (check-sat)
      )"},
      {kQfLia, CheckSatResult::Unsat, R"(
        (set-logic QF_LIA)
        (declare-fun x () Int)
        (assert (> x 0))
        (assert (< x 1))
        (check-sat)
      )"},
  };
}

std::vector<primo::core::TermId> representative_assertions(primo::smt::Frontend& frontend) {
  primo::core::TermManager& terms = frontend.terms();
  const primo::core::TermId p =
      frontend.fresh_internal_constant(terms.symbols().bool_sort(), "composition-p");
  const primo::core::TermId q =
      frontend.fresh_internal_constant(terms.symbols().bool_sort(), "composition-q");
  const primo::core::TermId x =
      frontend.fresh_internal_constant(terms.symbols().real_sort(), "composition-x");
  return {terms.mk_or({p, q}), terms.mk_le(x, terms.mk_rational(0))};
}

void production_profiles_have_stable_legal_structure() {
  for (const CompositionProfile& spec : profiles()) {
    primo::smt::Frontend frontend;
    const std::vector<primo::core::TermId> assertions = representative_assertions(frontend);
    primo::util::Statistics statistics;
    const primo::smt::PreprocessingProfile profile = primo::smt::analyze_preprocessing_profile(
        frontend.terms(), assertions, spec.options, false);
    const primo::smt::Strategy strategy =
        primo::smt::make_preprocessing_strategy(spec.options, profile, assertions);
    const std::string canonical = strategy.canonical();
    check(strategy.fingerprint() == spec.preprocessing_fingerprint);
    for (const std::string_view fragment : spec.required_canonical_fragments) {
      check(canonical.find(fragment) != std::string::npos);
    }
    for (const std::string_view fragment : spec.forbidden_canonical_fragments) {
      check(canonical.find(fragment) == std::string::npos);
    }

    const primo::smt::Strategy rebuilt =
        primo::smt::make_preprocessing_strategy(spec.options, profile, assertions);
    check(strategy == rebuilt);
    check(strategy.fingerprint() == rebuilt.fingerprint());

    primo::smt::TacticContext context(frontend.terms(), statistics, {}, true);
    const primo::smt::Strategy solver =
        primo::smt::make_session_solver_strategy(frontend, spec.options, statistics, context);
    if (spec.options.experimental_split_bools == 0) {
      check(solver.kind() == primo::smt::StrategyKind::Leaf);
      check(solver.leaf_id() == "solver-dispatch");
    } else {
      check(solver.kind() == primo::smt::StrategyKind::Sequence);
      check(solver.children().size() == 2);
      check(solver.children()[0].leaf_id() == "experimental-split-bools");
      check(solver.children()[0].parameters() ==
            std::vector<primo::smt::StrategyParameter>{
                {"k", std::to_string(spec.options.experimental_split_bools)}});
      check(solver.children()[1].leaf_id() == "solver-dispatch");
    }
  }
}

void qf_lia_profile_keeps_its_audited_leaf() {
  primo::smt::Frontend frontend;
  const std::vector<primo::core::TermId> assertions = representative_assertions(frontend);
  primo::util::Statistics statistics;
  for (const primo::smt::CheckSatOptions& options :
       {off_options(), basic_options(), strong_options()}) {
    const primo::smt::PreprocessingProfile profile =
        primo::smt::analyze_preprocessing_profile(frontend.terms(), assertions, options, true);
    check(profile.kind == primo::smt::PreprocessingProfileKind::QfLia);
    const std::string canonical =
        primo::smt::make_preprocessing_strategy(options, profile, assertions).canonical();
    check(canonical.find("qf-lia-solve-eqs") != std::string::npos);
    const bool explicit_preprocessing =
        options.preprocessing_mode != primo::smt::PreprocessingMode::Off;
    check((canonical.find("preprocessing-round") != std::string::npos) == explicit_preprocessing);
    check((canonical.find("\"negation-mode\" \"nnf\"") != std::string::npos) ==
          explicit_preprocessing);
    if (explicit_preprocessing) {
      check(canonical.find("\"gaussian-elimination\" \"false\"") != std::string::npos);
      check(canonical.find("\"ground-congruence\" \"true\"") == std::string::npos);
      check(canonical.find("\"ground-unit-substitution\" \"true\"") == std::string::npos);
      check(canonical.find("\"guarded-ground-unit-substitution\" \"true\"") == std::string::npos);
    }
  }
}

void real_profile_compositions_preserve_answers_models_and_counters() {
  for (const CompositionProfile& profile : profiles()) {
    for (const FormulaCase& formula : formula_cases()) {
      if ((profile.logics & formula.logic) == 0U) {
        continue;
      }
      primo::smt::CheckSatOptions options = profile.options;
      options.produce_model = true;
      const primo::smt::CheckSatOutcome outcome =
          primo::smt::check_sat_smtlib(formula.input, options);
      check(outcome.result == formula.expected);
      check(outcome.statistics.get(":tactic.preprocessing-profile.invocations") == 1);
      check(outcome.statistics.get(":tactic.normalize-boolean.invocations") >= 1);
      // QF_LIA deliberately overrides the general preprocessing profile with its audited
      // substitution-only path before the strategy is analyzed.
      if (formula.logic != kQfLia) {
        for (const std::string_view statistic : profile.required_statistics) {
          check(outcome.statistics.get(statistic) >= 1);
        }
      }
      if (formula.expected == primo::smt::CheckSatResult::Sat) {
        check(!outcome.model.empty());
      }
      if (profile.options.experimental_split_bools != 0) {
        check(outcome.statistics.get(":tactic.experimental-split-bools.invocations") == 1);
        check(outcome.statistics.get(":experimental.split-bools.output-goals") >= 2);
      }
    }
  }
}

}  // namespace

int main() {
  production_profiles_have_stable_legal_structure();
  qf_lia_profile_keeps_its_audited_leaf();
  real_profile_compositions_preserve_answers_models_and_counters();
  return 0;
}
