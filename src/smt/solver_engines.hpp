#pragma once

#include <string>
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
CheckSatOutcome solve_qf_uflia_engine(core::TermManager& terms,
                                      const std::vector<core::TermId>& assertions,
                                      const std::vector<core::TermId>& model_terms,
                                      const ModelConversionChain& conversions,
                                      const CheckSatOptions& options, util::Statistics& statistics);

// Session-boundary support whose implementation shares the solver engines'
// model and EUF traversal machinery. These functions do not own session state.
void record_ackermann_cost_diagnostics_for_session(const core::TermManager& terms,
                                                   const std::vector<core::TermId>& roots,
                                                   util::Statistics& statistics);
std::string build_qf_lra_preprocessing_only_model_for_session(
    core::TermManager& terms, const std::vector<core::TermId>& model_terms,
    const ModelConversionChain& conversions);

}  // namespace primo::smt
