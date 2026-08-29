#pragma once

#include <stdexcept>
#include <string>

namespace primo::smt {

// A well-formed request outside the implemented SMT fragment. This is a
// control-boundary exception, not a tactic alternative: solver-tactic adapters
// translate it into a typed inconclusive goal at their public boundary.
class UnsupportedInput : public std::runtime_error {
 public:
  explicit UnsupportedInput(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace primo::smt
