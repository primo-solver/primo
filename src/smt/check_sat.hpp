#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "util/statistics.hpp"

namespace primo::smt {

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
  bool uf_symmetry_breaking = false;
  bool eq_bridge = false;
  std::size_t preprocessing_passes = 1;
};

CheckSatOutcome check_sat_smtlib(std::string_view input, CheckSatOptions options = {});
std::string_view to_string(CheckSatResult result);

}  // namespace primo::smt
