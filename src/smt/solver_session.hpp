#pragma once

#include <string_view>

#include "smt/check_sat.hpp"

namespace primo::smt {

// Runs one parse/frontend/preprocess/solve session. Portfolio policies belong
// to the public check_sat coordinator and must not leak TermIds across calls.
CheckSatOutcome check_sat_smtlib_once(std::string_view input, CheckSatOptions options);

}  // namespace primo::smt
