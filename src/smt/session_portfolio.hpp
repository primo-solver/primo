#pragma once

#include <string_view>

#include "smt/check_sat.hpp"

namespace primo::smt {

// Session portfolios reparse from the original SMT-LIB input. Implementations
// must keep each arm isolated; a future parallel executor must therefore give
// every arm separate frontend, SAT, statistics, and cancellation resources.
class SessionExecutor {
 public:
  virtual ~SessionExecutor() = default;
  virtual CheckSatOutcome execute(std::string_view input, CheckSatOptions options) = 0;
};

class SequentialSessionExecutor final : public SessionExecutor {
 public:
  CheckSatOutcome execute(std::string_view input, CheckSatOptions options) override;
};

CheckSatOutcome execute_qf_uf_session_portfolio(std::string_view input, CheckSatOptions options,
                                                SessionExecutor& executor);

}  // namespace primo::smt
