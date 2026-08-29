#include "smt/check_sat.hpp"

#include <cstddef>
#include <string_view>
#include <utility>

#include "smt/solver_session.hpp"
#include "util/statistics.hpp"

namespace primo::smt {

CheckSatOutcome check_sat_smtlib(std::string_view input, CheckSatOptions options) {
  if (options.qf_uf_portfolio == QfUfPortfolioMode::Off) {
    return check_sat_smtlib_once(input, std::move(options));
  }

  const std::size_t portfolio_conflicts = options.qf_uf_portfolio_conflicts;
  CheckSatOptions first_options = options;
  first_options.qf_uf_portfolio = QfUfPortfolioMode::Off;
  first_options.preprocessing_mode = PreprocessingMode::Basic;
  first_options.theory_phase = TheoryPhaseMode::Polarity;
  first_options.max_theory_conflicts = portfolio_conflicts;
  CheckSatOutcome first = check_sat_smtlib_once(input, std::move(first_options));
  first.statistics.set(":portfolio.qf-uf.attempts", 1);
  first.statistics.set(":portfolio.qf-uf.basic-polarity.attempted", 1);
  first.statistics.set(":portfolio.qf-uf.basic-polarity.conflict-budget", portfolio_conflicts);
  first.statistics.set(":portfolio.qf-uf.basic-polarity.first-conflicts",
                       first.statistics.get(util::StatKey::TheoryConflicts));
  if (!(first.result == CheckSatResult::Unknown &&
        first.message == "theory conflict limit reached")) {
    first.statistics.set(":portfolio.qf-uf.basic-polarity.solved", 1);
    return first;
  }

  CheckSatOptions fallback_options = std::move(options);
  fallback_options.qf_uf_portfolio = QfUfPortfolioMode::Off;
  CheckSatOutcome fallback = check_sat_smtlib_once(input, std::move(fallback_options));
  fallback.statistics.set(":portfolio.qf-uf.attempts", 2);
  fallback.statistics.set(":portfolio.qf-uf.basic-polarity.attempted", 1);
  fallback.statistics.set(":portfolio.qf-uf.basic-polarity.fell-back", 1);
  fallback.statistics.set(":portfolio.qf-uf.basic-polarity.conflict-budget", portfolio_conflicts);
  fallback.statistics.set(":portfolio.qf-uf.basic-polarity.first-conflicts",
                          first.statistics.get(util::StatKey::TheoryConflicts));
  return fallback;
}

std::string_view to_string(CheckSatResult result) {
  switch (result) {
    case CheckSatResult::Sat:
      return "sat";
    case CheckSatResult::Unsat:
      return "unsat";
    case CheckSatResult::Unknown:
      return "unknown";
    case CheckSatResult::Unsupported:
      return "unsupported";
  }
  return "unknown";
}

}  // namespace primo::smt
