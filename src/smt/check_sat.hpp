#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "util/statistics.hpp"

namespace primo::smt {

enum class QfUfPreprocessMode {
  Off,
  Basic,
  Strong,
  Auto,
};

enum class EufPropagationMode {
  Off,
  Model,
  Online,
};

enum class EufEngine {
  Lazy,
  Incremental,
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
  QfUfPreprocessMode qf_uf_preprocess = QfUfPreprocessMode::Basic;
  EufEngine euf_engine = EufEngine::Incremental;
  EufPropagationMode euf_propagation = EufPropagationMode::Off;
  AutoBudget euf_propagation_budget;
  AutoBudget euf_propagation_delivery;
  bool uf_symmetry_breaking = false;
  bool eq_bridge = false;
  bool ground_congruence = false;
  std::size_t euf_propagation_interval = 1;
  double euf_propagation_assign_threshold = 1.0;
  std::size_t preprocessing_passes = 1;
};

CheckSatOutcome check_sat_smtlib(std::string_view input, CheckSatOptions options = {});
std::string_view to_string(CheckSatResult result);

}  // namespace primo::smt
