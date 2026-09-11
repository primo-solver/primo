#include "sat/ipasir_up.hpp"

#include <vector>

namespace primo::sat {

void IpasirUpBackend::reconnect_external_propagator(ExternalPropagator& propagator,
                                                    const std::vector<int>& observed_variables) {
  disconnect_external_propagator();
  connect_external_propagator(&propagator);
  for (const int variable : observed_variables) add_observed_var(variable);
}

}  // namespace primo::sat
