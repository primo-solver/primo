#pragma once

#include <vector>

#include "core/ids.hpp"
#include "smt/check_sat.hpp"
#include "smt/model_reconstruction.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::util {
class Statistics;
}

namespace primo::smt {

CheckSatOutcome solve_boolean_conjunction(
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    const CheckSatOptions& options, util::Statistics& statistics,
    const std::vector<TheoryModelAssignment>& preprocessing_model_assignments);

}  // namespace primo::smt
