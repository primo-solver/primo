#pragma once

#include <cstddef>
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
  AutoBudget euf_propagation_budget;
  AutoBudget euf_propagation_delivery{AutoBudget::Kind::Fixed, 1000};
  bool uf_symmetry_breaking = true;
  bool finite_domain_value_precedence = true;
  bool finite_domain_equality_definitions = true;
  bool auto_finite_domain_equality_definitions = false;
  bool eq_bridge = true;
  bool ground_congruence = false;
  bool ground_unit_substitution = false;
  bool guarded_ground_unit_substitution = false;
  bool solved_constant_substitution = true;
  bool preprocess_only = false;
  std::size_t euf_propagation_interval = 32;
  double euf_propagation_assign_threshold = 0.25;
  std::size_t preprocessing_passes = 4;
  std::size_t preprocessing_adaptive_extra_passes = 4;
  std::size_t preprocessing_adaptive_shrink_percent = 10;
  std::size_t finite_domain_eqdefs_auto_domain_limit = 8;
  std::size_t finite_domain_eqdefs_auto_clause_limit = 5000;
  std::size_t finite_domain_eqdefs_auto_term_limit = 5000;
  std::vector<std::pair<std::string, int>> cadical_options;
  util::Statistics* live_statistics = nullptr;
};

CheckSatOutcome check_sat_smtlib(std::string_view input, CheckSatOptions options = {});
std::string_view to_string(CheckSatResult result);

}  // namespace primo::smt
