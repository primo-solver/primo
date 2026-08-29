#include "smt/solver_runtime.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <utility>

#include "util/statistics.hpp"

namespace primo::smt {

SolveTerminator::SolveTerminator(std::function<bool()> cancelled, util::Statistics& statistics,
                                 std::optional<std::size_t> conflict_limit)
    : cancelled_(std::move(cancelled)), statistics_(statistics), conflict_limit_(conflict_limit) {}

bool SolveTerminator::terminate() {
  if (cancelled_ && cancelled_()) {
    cancelled_reached_ = true;
    return true;
  }
  if (conflict_limit_.has_value() &&
      statistics_.get(util::StatKey::TheoryConflicts) >= conflict_limit_.value()) {
    conflict_limit_reached_ = true;
    return true;
  }
  return false;
}

ScopedTerminatorConnection::ScopedTerminatorConnection(sat::IpasirUpBackend& backend,
                                                       sat::Terminator& terminator)
    : backend_(backend) {
  backend_.connect_terminator(&terminator);
}

ScopedTerminatorConnection::~ScopedTerminatorConnection() { backend_.disconnect_terminator(); }

}  // namespace primo::smt
