#pragma once

#include <vector>

#include "sat/ipasir_up.hpp"
#include "smt/check_sat.hpp"
#include "smt/clausifier.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::util {
class Statistics;
}

namespace primo::smt {

CheckSatResult map_sat_result(sat::SatResult result);
void publish_sat_stats(const sat::BackendStatistics& backend, util::Statistics& statistics);
void publish_theory_atom_purity_stats(const core::TermManager& terms,
                                      const std::vector<BooleanClausifier::TheoryAtom>& atoms,
                                      util::Statistics& statistics);

}  // namespace primo::smt
