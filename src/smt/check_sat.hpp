#pragma once

#include <string>
#include <string_view>

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
};

CheckSatOutcome check_sat_smtlib(std::string_view input);
std::string_view to_string(CheckSatResult result);

}  // namespace primo::smt
