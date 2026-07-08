#pragma once

#include "sat/ipasir_up.hpp"

namespace primo::sat {

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

 private:
  void* solver_ = nullptr;
  int max_var_ = 0;
  SatResult last_result_ = SatResult::Unknown;
};

}  // namespace primo::sat
