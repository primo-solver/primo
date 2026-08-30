#include "smt/check_sat.hpp"

#include <string_view>
#include <utility>

#include "smt/session_portfolio.hpp"

namespace primo::smt {

CheckSatOutcome check_sat_smtlib(std::string_view input, CheckSatOptions options) {
  SequentialSessionExecutor executor;
  if (options.qf_uf_portfolio == QfUfPortfolioMode::Off) {
    return executor.execute(input, std::move(options));
  }
  return execute_qf_uf_session_portfolio(input, std::move(options), executor);
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
