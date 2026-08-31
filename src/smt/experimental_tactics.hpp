#pragma once

#include <cstddef>
#include <vector>

#include "smt/strategy.hpp"
#include "smt/tactic.hpp"

namespace primo::smt {

// This bound protects the explicit 2^k materialization performed by the
// experimental splitter. It is an engineering guard, not a logical limit.
inline constexpr std::size_t kMaxExperimentalSplitBools = 16;

struct ExperimentalSplitBoolsOptions {
  std::size_t max_bools = 0;

  std::vector<StrategyParameter> strategy_parameters() const;
};

// Exhaustively partitions each inconclusive input goal over up to max_bools
// Boolean atoms. The returned alternatives are ordered false-first.
Tactic make_experimental_split_bools(ExperimentalSplitBoolsOptions options);

}  // namespace primo::smt
