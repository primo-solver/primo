#pragma once

#include <string_view>

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
  ~CadicalBackend() override;

  CadicalBackend(const CadicalBackend&) = delete;
  CadicalBackend& operator=(const CadicalBackend&) = delete;

  int new_var() override;
  void add_clause(const Clause& clause) override;
  void assume(Lit lit) override;
  SatResult solve() override;
  std::optional<bool> value(int var) const override;
  bool failed(Lit lit) const override;
  void connect_external_propagator(CaDiCaL::ExternalPropagator* propagator);
  void disconnect_external_propagator();
  void add_observed_var(int var);

 private:
  CaDiCaL::Solver* solver_ = nullptr;
  int max_var_ = 0;
  SatResult last_result_ = SatResult::Unknown;
};

}  // namespace primo::sat
