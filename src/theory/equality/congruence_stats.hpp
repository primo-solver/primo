#pragma once

#include <cstddef>

namespace primo::theory::equality {

struct IncrementalCongruenceClosureStats {
  std::size_t add_term_requests = 0;
  std::size_t add_term_seen_hits = 0;
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
  std::size_t explain_cache_bound_misses = 0;
  std::size_t explain_length = 0;
  std::size_t explain_top_level_queries = 0;
  std::size_t explain_recursive_queries = 0;
  std::size_t explain_repeated_pair_queries = 0;
  std::size_t explain_max_recursion_depth = 0;
  std::size_t explain_bfs_searches = 0;
  std::size_t explain_bfs_visited = 0;
  std::size_t explain_bfs_path_edges = 0;
  std::size_t explain_bfs_max_queue = 0;
  std::size_t explain_proof_forest_queries = 0;
  std::size_t explain_proof_forest_path_edges = 0;
  std::size_t explain_proof_forest_max_path_edges = 0;
  std::size_t explain_proof_forest_missing_lca = 0;
  std::size_t explain_proof_forest_shorter_than_bfs = 0;
  std::size_t explain_proof_forest_equal_to_bfs = 0;
  std::size_t explain_proof_forest_longer_than_bfs = 0;
  std::size_t explain_causal_edge_skips = 0;
  std::size_t explain_direct_edges = 0;
  std::size_t explain_congruence_edges = 0;
  std::size_t explain_reason_duplicate_literals = 0;
  std::size_t conflict_reason_max_length = 0;
  std::size_t conflict_reason_length_le_4 = 0;
  std::size_t conflict_reason_length_le_8 = 0;
  std::size_t conflict_reason_length_le_16 = 0;
  std::size_t conflict_reason_length_le_32 = 0;
  std::size_t conflict_reason_length_le_64 = 0;
  std::size_t conflict_reason_length_gt_64 = 0;
  std::size_t backtracks = 0;
  std::size_t trail_entries = 0;
  std::size_t class_member_trail_entries_saved = 0;
  std::size_t use_list_appends = 0;
  std::size_t use_list_duplicate_skips = 0;
  std::size_t affected_use_list_dedup = 0;
  std::size_t max_use_list_size = 0;
  std::size_t changed_term_drains = 0;
  std::size_t changed_terms_delivered = 0;
  std::size_t changed_term_generation_rebases = 0;
  std::size_t max_checkpoint_depth = 0;
};

}  // namespace primo::theory::equality
