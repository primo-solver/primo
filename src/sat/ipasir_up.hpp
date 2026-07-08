#pragma once

#include <optional>

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
  virtual int new_var() = 0;
  virtual void add_clause(const Clause& clause) = 0;
  virtual void assume(Lit lit) = 0;
  virtual SatResult solve() = 0;
  virtual std::optional<bool> value(int var) const = 0;
  virtual bool failed(Lit lit) const = 0;
};

}  // namespace primo::sat
