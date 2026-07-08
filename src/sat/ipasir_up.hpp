#pragma once

#include "sat/clause.hpp"

namespace primo::sat {

enum class SatResult {
  Sat,
  Unsat,
  Unknown,
  Unsupported,
};

class IpasirUpBackend {
 public:
  virtual ~IpasirUpBackend() = default;
  virtual void add_clause(const Clause& clause) = 0;
  virtual SatResult solve() = 0;
};

}  // namespace primo::sat
