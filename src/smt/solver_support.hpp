#pragma once

#include <vector>

#include "sat/ipasir_up.hpp"
#include "smt/check_sat.hpp"
#include "smt/clausifier.hpp"
#include "smt/tactic.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::util {
class Statistics;
}

namespace primo::smt {

// An AnyOf is disjunctive, so a preprocessing result cannot be reduced to its
// first alternative: a formula satisfiable only through a later alternative
// would be reported unsat. This splits one preprocessed AnyOf into the
// decisions the session has to make about it.
struct PreprocessedAlternatives {
  // Alternatives that did not reduce to false, in their original order. Empty
  // means every alternative is unsatisfiable, so the goal is unsat.
  std::vector<Goal> surviving;
  // Set when any alternative carries an inconclusive reason. Dropping such an
  // alternative could lose the only satisfiable disjunct, so the session must
  // report the interruption rather than solve the rest.
  bool any_inconclusive = false;
};

PreprocessedAlternatives triage_preprocessed_alternatives(const core::TermManager& terms,
                                                          const AnyOf& result);

CheckSatResult map_sat_result(sat::SatResult result);
void publish_sat_stats(const sat::BackendStatistics& backend, util::Statistics& statistics);
void publish_theory_atom_purity_stats(const core::TermManager& terms,
                                      const std::vector<BooleanClausifier::TheoryAtom>& atoms,
                                      util::Statistics& statistics);

}  // namespace primo::smt
