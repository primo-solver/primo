#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "core/ids.hpp"
#include "smt/check_sat.hpp"
#include "smt/preprocessing.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::smt {

bool contains_application_term(const core::TermManager& terms, core::TermId root);

struct FormulaFeatureSummary {
  bool has_application_term = false;
  bool has_equality = false;
  bool has_arithmetic = false;
  bool has_euf_predicate = false;

  FormulaFeatureSummary& operator|=(const FormulaFeatureSummary& other);
};

FormulaFeatureSummary summarize_formula_features(
    const core::TermManager& terms, core::TermId root,
    std::unordered_map<core::TermId, FormulaFeatureSummary>& memo, MixedDispatchMode dispatch);

struct BooleanShape {
  std::size_t or_nodes = 0;
  std::size_t not_nodes = 0;
  std::size_t top_level_or_nodes = 0;
  std::size_t max_top_level_or_children = 0;
};

BooleanShape boolean_shape(const core::TermManager& terms,
                           const std::vector<core::TermId>& assertions);
bool should_use_auto_top_level_or_tseitin(const CheckSatOptions& options,
                                          const FiniteDomainAtMostOneResult& finite_domain,
                                          const BooleanShape& shape);

}  // namespace primo::smt
