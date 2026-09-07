#pragma once

#include <string>

#include "smt/tactic.hpp"

namespace primo::smt {

enum class SolverStatus {
  Sat,
  Unsat,
  Inconclusive,
};

// Complete solver leaves use this internal value. Expected operational exits
// are typed; diagnostics are presentation text and never drive control flow.
struct SolverAnswer {
  SolverStatus status = SolverStatus::Inconclusive;
  InconclusiveReason reason = InconclusiveReason::Unknown;
  std::string diagnostic;
  std::string model;
  std::shared_ptr<const core::Model> typed_model = {};
};

}  // namespace primo::smt
