#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sat/ipasir_up.hpp"

namespace CaDiCaL {
class ExternalPropagator;
class Solver;
}  // namespace CaDiCaL

namespace primo::sat {

std::string_view cadical_signature();

class CadicalBackend final : public IpasirUpBackend {
 public:
  CadicalBackend();
  explicit CadicalBackend(const std::vector<std::pair<std::string, int>>& options);
  ~CadicalBackend() override;

  CadicalBackend(const CadicalBackend&) = delete;
  CadicalBackend& operator=(const CadicalBackend&) = delete;

  int new_var() override;
  void add_clause(const Clause& clause) override;
  void assume(Lit lit) override;
  SatResult solve() override;
  std::optional<bool> value(int var) const override;
  bool failed(Lit lit) const override;
  BackendStatistics statistics() const override;
  void phase(Lit lit);
  void connect_external_propagator(CaDiCaL::ExternalPropagator* propagator);
  void disconnect_external_propagator();
  void add_observed_var(int var);

 private:
  CaDiCaL::Solver* solver_ = nullptr;
  int max_var_ = 0;
  SatResult last_result_ = SatResult::Unknown;
  BackendStatistics statistics_;
};

}  // namespace primo::sat
