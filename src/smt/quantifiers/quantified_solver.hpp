#pragma once

#include <functional>
#include <string_view>
#include <vector>

#include "core/ids.hpp"
#include "smt/check_sat.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::util {
class Statistics;
}

namespace primo::smt {

// Solves assertions containing quantified formulas in logic UF with the online
// component driver: the existing EUF component plus the quantifier component,
// which queues Skolem and enumerative instance lemmas (docs/quantifiers.md).
// Answers unsat; sat only when every assigned quantifier is covered by a Skolem
// lemma; otherwise unknown. Models are not produced.
CheckSatOutcome solve_quantified_uf(
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    const CheckSatOptions& options, util::Statistics& statistics,
    const std::function<core::TermId(core::SortId, std::string_view)>& fresh_constant);

}  // namespace primo::smt
