#include "smt/session_portfolio.hpp"

#include <cstddef>
#include <string_view>
#include <utility>

#include "smt/check_sat.hpp"
#include "smt/inconclusive_reason.hpp"
#include "smt/solver_session.hpp"
#include "util/statistics.hpp"

namespace primo::smt {

CheckSatOutcome SequentialSessionExecutor::execute(std::string_view input,
                                                   CheckSatOptions options) {
  return check_sat_smtlib_once(input, std::move(options));
}

CheckSatOutcome execute_qf_uf_session_portfolio(std::string_view input, CheckSatOptions options,
                                                SessionExecutor& executor) {
  const std::size_t portfolio_conflicts = options.qf_uf_portfolio_conflicts;
  CheckSatOptions first_options = options;
  first_options.qf_uf_portfolio = QfUfPortfolioMode::Off;
  first_options.preprocessing_mode = PreprocessingMode::Basic;
  first_options.theory_phase = TheoryPhaseMode::Polarity;
  first_options.max_theory_conflicts = portfolio_conflicts;
  CheckSatOutcome first = executor.execute(input, std::move(first_options));
  first.statistics.set(":portfolio.qf-uf.attempts", 1);
  first.statistics.set(":portfolio.qf-uf.basic-polarity.attempted", 1);
  first.statistics.set(":portfolio.qf-uf.basic-polarity.conflict-budget", portfolio_conflicts);
  first.statistics.set(":portfolio.qf-uf.basic-polarity.first-conflicts",
                       first.statistics.get(util::StatKey::TheoryConflicts));
  if (first.result != CheckSatResult::Unknown ||
      first.reason != InconclusiveReason::ResourceLimit) {
    first.statistics.set(":portfolio.qf-uf.basic-polarity.solved", 1);
    return first;
  }

  CheckSatOptions fallback_options = std::move(options);
  fallback_options.qf_uf_portfolio = QfUfPortfolioMode::Off;
  CheckSatOutcome fallback = executor.execute(input, std::move(fallback_options));
  fallback.statistics.set(":portfolio.qf-uf.attempts", 2);
  fallback.statistics.set(":portfolio.qf-uf.basic-polarity.attempted", 1);
  fallback.statistics.set(":portfolio.qf-uf.basic-polarity.fell-back", 1);
  fallback.statistics.set(":portfolio.qf-uf.basic-polarity.conflict-budget", portfolio_conflicts);
  fallback.statistics.set(":portfolio.qf-uf.basic-polarity.first-conflicts",
                          first.statistics.get(util::StatKey::TheoryConflicts));
  return fallback;
}

}  // namespace primo::smt
