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

// Complete single-goal engines used by the formula-valued solver leaves.
// These functions own no session state; all execution capabilities arrive via
// CheckSatOptions and Statistics.
CheckSatOutcome solve_qf_uf_engine(core::TermManager& terms,
                                   const std::vector<core::TermId>& assertions,
                                   const CheckSatOptions& options, util::Statistics& statistics,
                                   const ModelConversionChain& conversions);
CheckSatOutcome solve_qf_lra_engine(core::TermManager& terms,
                                    const std::vector<core::TermId>& assertions,
                                    const std::vector<core::TermId>& model_terms,
                                    const ModelConversionChain& conversions,
                                    const CheckSatOptions& options, util::Statistics& statistics);
CheckSatOutcome solve_qf_lia_engine(core::TermManager& terms,
                                    const std::vector<core::TermId>& assertions,
                                    const std::vector<core::TermId>& model_terms,
                                    const CheckSatOptions& options, util::Statistics& statistics,
                                    const ModelConversionChain& conversions);
CheckSatOutcome solve_qf_uflra_engine(core::TermManager& terms,
                                      const std::vector<core::TermId>& assertions,
                                      const std::vector<core::TermId>& model_terms,
                                      const ModelConversionChain& conversions,
                                      const CheckSatOptions& options, util::Statistics& statistics);

}  // namespace primo::smt
