#pragma once

namespace primo::smt {

enum class CheckSatResult {
  Sat,
  Unsat,
  Unknown,
  Unsupported,
};

}  // namespace primo::smt
