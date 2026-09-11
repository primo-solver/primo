#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "core/ids.hpp"
#include "smt/check_sat.hpp"

namespace primo::smt {

class Frontend;

// Runs one parse/frontend/preprocess/solve session. Portfolio policies belong
// to the public check_sat coordinator and must not leak TermIds across calls.
CheckSatOutcome check_sat_smtlib_once(std::string_view input, CheckSatOptions options);

// Consumes a never-preprocessed frontend and IDs owned by that exact frontend.
// Returned models own their signatures; no borrowed query state escapes.
CheckSatOutcome check_sat_frontend_once(std::unique_ptr<Frontend> frontend,
                                        std::vector<core::TermId> assertions,
                                        CheckSatOptions options);

}  // namespace primo::smt
