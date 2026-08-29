#pragma once

#include <cstddef>
#include <functional>
#include <optional>

#include "sat/ipasir_up.hpp"

namespace primo::util {
class Statistics;
}

namespace primo::smt {

// Per-solve cancellation/resource terminator. The object and its connection
// must be owned in the same stack frame as the SAT backend.
class SolveTerminator final : public sat::Terminator {
 public:
  SolveTerminator(std::function<bool()> cancelled, util::Statistics& statistics,
                  std::optional<std::size_t> conflict_limit = std::nullopt);

  bool terminate() override;
  bool cancelled_reached() const { return cancelled_reached_; }
  bool conflict_limit_reached() const { return conflict_limit_reached_; }

 private:
  std::function<bool()> cancelled_;
  util::Statistics& statistics_;
  std::optional<std::size_t> conflict_limit_;
  bool cancelled_reached_ = false;
  bool conflict_limit_reached_ = false;
};

class ScopedTerminatorConnection {
 public:
  ScopedTerminatorConnection(sat::IpasirUpBackend& backend, sat::Terminator& terminator);
  ~ScopedTerminatorConnection();

  ScopedTerminatorConnection(const ScopedTerminatorConnection&) = delete;
  ScopedTerminatorConnection& operator=(const ScopedTerminatorConnection&) = delete;

 private:
  sat::IpasirUpBackend& backend_;
};

}  // namespace primo::smt
