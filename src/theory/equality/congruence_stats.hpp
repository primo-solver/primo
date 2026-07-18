#pragma once

#include <cstddef>

namespace primo::theory::equality {

struct IncrementalCongruenceClosureStats {
  std::size_t terms = 0;
  std::size_t flat_definitions = 0;
  std::size_t merges = 0;
  std::size_t pending_pops = 0;
  std::size_t lookup_probes = 0;
  std::size_t lookup_hits = 0;
  std::size_t congruence_merges = 0;
  std::size_t disequality_checks = 0;
  std::size_t conflicts = 0;
  std::size_t explain_calls = 0;
  std::size_t explain_cache_hits = 0;
  std::size_t explain_length = 0;
  std::size_t backtracks = 0;
  std::size_t use_list_appends = 0;
  std::size_t use_list_duplicate_skips = 0;
  std::size_t affected_use_list_dedup = 0;
  std::size_t max_use_list_size = 0;
};

}  // namespace primo::theory::equality
