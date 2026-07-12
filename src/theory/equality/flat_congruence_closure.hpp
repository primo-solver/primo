#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

#include "sat/lit.hpp"
#include "theory/equality/congruence_stats.hpp"
#include "theory/equality/flat_terms.hpp"

namespace primo::theory::equality {

class FlatCongruenceClosure {
 public:
  explicit FlatCongruenceClosure(std::size_t node_count = 0);

  std::size_t push();
  void pop(std::size_t checkpoint);
  void ensure_node_count(std::size_t node_count);
  void add_definition(const FlatDefinition& definition);
  void assert_equal(FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit> reason = {});
  void assert_distinct(FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit> reason = {});
  bool are_equal(FlatTermId lhs, FlatTermId rhs);
  std::optional<std::vector<sat::Lit>> explain_equal(FlatTermId lhs, FlatTermId rhs) const;
  std::optional<std::vector<sat::Lit>> conflict() const { return conflict_; }
  const IncrementalCongruenceClosureStats& stats() const { return stats_; }

 private:
  // Section-3-style congruence closure state from Nieuwenhuis and Oliveras:
  // eager representatives, class/use lists, signature lookup, and pending merges.
  struct ApplyKey {
    FlatTermId function = kInvalidFlatTermId;
    FlatTermId argument = kInvalidFlatTermId;

    bool operator==(const ApplyKey& other) const = default;
  };

  struct ApplyKeyHash {
    std::size_t operator()(const ApplyKey& key) const;
  };

  struct Definition {
    FlatTermId function = kInvalidFlatTermId;
    FlatTermId argument = kInvalidFlatTermId;
    FlatTermId result = kInvalidFlatTermId;
  };

  struct PendingMerge {
    FlatTermId lhs = kInvalidFlatTermId;
    FlatTermId rhs = kInvalidFlatTermId;
    std::vector<sat::Lit> reason;
    bool congruence = false;
    Definition lhs_definition;
    Definition rhs_definition;
  };

  struct Disequality {
    FlatTermId lhs = kInvalidFlatTermId;
    FlatTermId rhs = kInvalidFlatTermId;
    std::vector<sat::Lit> reason;
  };

  struct Edge {
    FlatTermId lhs = kInvalidFlatTermId;
    FlatTermId rhs = kInvalidFlatTermId;
    std::vector<sat::Lit> reason;
    bool congruence = false;
    Definition lhs_definition;
    Definition rhs_definition;
  };

  struct Snapshot {
    std::vector<FlatTermId> parent;
    std::vector<std::size_t> class_size;
    std::vector<std::vector<FlatTermId>> class_members;
    std::vector<std::vector<FlatTermId>> use_lists;
    std::vector<Definition> definitions;
    std::unordered_map<FlatTermId, std::size_t> definition_by_result;
    std::unordered_map<ApplyKey, FlatTermId, ApplyKeyHash> lookup;
    std::vector<PendingMerge> pending;
    std::vector<Disequality> disequalities;
    std::vector<std::vector<std::size_t>> disequality_lists;
    std::vector<Edge> edges;
    std::vector<std::vector<std::size_t>> edge_adjacency;
    std::optional<std::vector<sat::Lit>> conflict;
    IncrementalCongruenceClosureStats stats;
  };

  FlatTermId representative(FlatTermId term) const;
  ApplyKey signature(const Definition& definition) const;
  void enqueue(FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit> reason, bool congruence);
  void enqueue_congruence(const Definition& lhs, const Definition& rhs);
  void propagate();
  void merge_classes(FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit> reason, bool congruence);
  void merge_classes(PendingMerge pending);
  void process_use_list(FlatTermId old_repr, FlatTermId new_repr);
  bool check_disequality(const Disequality& disequality);
  void check_disequalities_touching(FlatTermId lhs_repr, FlatTermId rhs_repr);
  void begin_reason_collection(std::vector<sat::Lit>& reason) const;
  void append_unique_reason(std::vector<sat::Lit>& target,
                            const std::vector<sat::Lit>& source) const;
  void append_edge_reason(const Edge& edge, std::vector<sat::Lit>& reason) const;
  bool explain_equal_into(FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit>& reason) const;

  std::vector<FlatTermId> parent_;
  std::vector<std::size_t> class_size_;
  std::vector<std::vector<FlatTermId>> class_members_;
  std::vector<std::vector<FlatTermId>> use_lists_;
  std::vector<Definition> definitions_;
  std::unordered_map<FlatTermId, std::size_t> definition_by_result_;
  std::unordered_map<ApplyKey, FlatTermId, ApplyKeyHash> lookup_;
  std::vector<PendingMerge> pending_;
  std::vector<Disequality> disequalities_;
  std::vector<std::vector<std::size_t>> disequality_lists_;
  std::vector<Edge> edges_;
  std::vector<std::vector<std::size_t>> edge_adjacency_;
  std::vector<Snapshot> snapshots_;
  std::optional<std::vector<sat::Lit>> conflict_;
  IncrementalCongruenceClosureStats stats_;
  mutable std::vector<std::size_t> visit_marks_;
  mutable std::vector<std::size_t> parent_edge_scratch_;
  mutable std::vector<FlatTermId> parent_scratch_;
  mutable std::vector<FlatTermId> queue_scratch_;
  mutable std::size_t visit_generation_ = 0;
  mutable std::vector<std::size_t> reason_marks_;
  mutable std::size_t reason_generation_ = 0;
};

}  // namespace primo::theory::equality
