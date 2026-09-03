#include "smt/solver_support.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "util/statistics.hpp"

namespace primo::smt {

PreprocessedAlternatives triage_preprocessed_alternatives(const core::TermManager& terms,
                                                          const AnyOf& result) {
  PreprocessedAlternatives triaged;
  triaged.surviving.reserve(result.size());
  for (const Goal& alternative : result.alternatives()) {
    if (alternative.reason() != InconclusiveReason::None) {
      triaged.any_inconclusive = true;
      continue;
    }
    const bool is_false =
        std::ranges::any_of(alternative.assertions(), [&terms](core::TermId assertion) {
          const core::Term& node = terms.get(assertion);
          return node.kind == core::TermKind::BoolValue && !node.bool_value;
        });
    if (!is_false) {
      triaged.surviving.push_back(alternative);
    }
  }
  return triaged;
}

CheckSatResult map_sat_result(sat::SatResult result) {
  switch (result) {
    case sat::SatResult::Sat:
      return CheckSatResult::Sat;
    case sat::SatResult::Unsat:
      return CheckSatResult::Unsat;
    case sat::SatResult::Unknown:
      return CheckSatResult::Unknown;
    case sat::SatResult::Unsupported:
      return CheckSatResult::Unsupported;
  }
  return CheckSatResult::Unknown;
}

void publish_sat_stats(const sat::BackendStatistics& backend, util::Statistics& statistics) {
  statistics.set(util::StatKey::SatVars, backend.variables);
  statistics.set(util::StatKey::SatClauses, backend.clauses);
  statistics.set(util::StatKey::SatClauseLits, backend.clause_literals);
  statistics.set(util::StatKey::SatUnitClauses, backend.unit_clauses);
  statistics.set(util::StatKey::SatBinaryClauses, backend.binary_clauses);
  statistics.set(util::StatKey::SatConflicts, backend.conflicts);
  statistics.set(util::StatKey::SatDecisions, backend.decisions);
  statistics.set(util::StatKey::SatPropagations, backend.propagations);
}

void publish_theory_atom_purity_stats(const core::TermManager& terms,
                                      const std::vector<BooleanClausifier::TheoryAtom>& atoms,
                                      util::Statistics& statistics) {
  std::size_t positive = 0;
  std::size_t negative = 0;
  std::size_t predicates = 0;
  for (const BooleanClausifier::TheoryAtom& atom : atoms) {
    positive += atom.occurs_positive && !atom.occurs_negative ? 1U : 0U;
    negative += atom.occurs_negative && !atom.occurs_positive ? 1U : 0U;
    const core::Term& term = terms.get(atom.term);
    predicates +=
        term.kind == core::TermKind::Application && term.sort == terms.symbols().bool_sort() ? 1U
                                                                                             : 0U;
  }
  statistics.set(util::StatKey::PureTheoryAtomsPositive, positive);
  statistics.set(util::StatKey::PureTheoryAtomsNegative, negative);
  statistics.set(util::StatKey::EufPredicateAtoms, predicates);
}

}  // namespace primo::smt
