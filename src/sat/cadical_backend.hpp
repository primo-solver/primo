#pragma once

#include "sat/ipasir_up.hpp"

namespace primo::sat {

class CadicalBackend final : public IpasirUpBackend {
 public:
  void add_clause(const Clause& clause) override;
  SatResult solve() override;
};

}  // namespace primo::sat
