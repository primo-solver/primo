#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

#include "sat/lit.hpp"
#include "theory/equality/congruence_stats.hpp"
#include "theory/equality/flat_terms.hpp"

namespace primo::theory::equality {

class FlatCongruenceClosureTestAccess;

enum class FlatExplanationSearchMode {
  OneSidedBfs,
  BidirectionalBfs,
};

class FlatCongruenceClosure {
 public:
  explicit FlatCongruenceClosure(
      std::size_t node_count = 0,
      FlatExplanationSearchMode explanation_search_mode = FlatExplanationSearchMode::OneSidedBfs);

  std::size_t push();
  void pop(std::size_t checkpoint);
  void ensure_node_count(std::size_t node_count);
  void add_definition(const FlatDefinition& definition);
  void assert_equal(FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit> reason = {});
  void assert_distinct(FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit> reason = {});
  bool are_equal(FlatTermId lhs, FlatTermId rhs);
  std::optional<std::vector<sat::Lit>> explain_equal(FlatTermId lhs, FlatTermId rhs) const;
  std::optional<std::vector<sat::Lit>> conflict() const { return conflict_; }
  void drain_changed_terms(std::vector<FlatTermId>& out);
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

  struct ExplanationKey {
    FlatTermId lhs = kInvalidFlatTermId;
    FlatTermId rhs = kInvalidFlatTermId;

    bool operator==(const ExplanationKey& other) const = default;
  };

  struct ExplanationKeyHash {
    std::size_t operator()(const ExplanationKey& key) const;
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

  enum class TrailKind {
    Parent,
    ClassSize,
    ClassMembersMerge,
    UseListRestore,
    UseListPop,
    DisequalityListRestore,
    DisequalityListPop,
    EdgeAdjacencyPop,
    LookupInsertion,
    DefinitionByResultInsertion,
  };

  struct TrailEntry {
    TrailKind kind = TrailKind::Parent;
    std::size_t index = 0;
    FlatTermId flat_value = kInvalidFlatTermId;
    std::size_t size_value = 0;
    std::vector<FlatTermId> flat_vector;
    std::vector<std::size_t> size_vector;
    ApplyKey lookup_key;
  };

  struct Checkpoint {
    std::size_t node_count = 0;
    std::size_t definitions_size = 0;
    std::size_t pending_size = 0;
    std::size_t disequalities_size = 0;
    std::size_t edges_size = 0;
    std::size_t changed_terms_size = 0;
    std::size_t trail_size = 0;
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
  void append_use_tracked(FlatTermId representative, FlatTermId result);
  void push_parent_change(FlatTermId term);
  void push_class_size_change(FlatTermId term);
  void restore_trail_entry(TrailEntry& entry);
  bool check_disequality(const Disequality& disequality);
  void check_disequalities_touching(FlatTermId lhs_repr, FlatTermId rhs_repr);
  void begin_reason_collection(std::vector<sat::Lit>& reason) const;
  void append_unique_reason(std::vector<sat::Lit>& target,
                            const std::vector<sat::Lit>& source) const;
  void record_conflict_reason_length(std::size_t length);
  ExplanationKey explanation_key(FlatTermId lhs, FlatTermId rhs) const;
  bool append_edge_reason(std::size_t edge_index, std::vector<sat::Lit>& reason) const;
  bool explain_equal_into(FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit>& reason,
                          std::size_t max_edge_index) const;

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
  std::vector<FlatTermId> changed_terms_;
  std::vector<std::vector<std::size_t>> edge_adjacency_;
  std::vector<Checkpoint> checkpoints_;
  std::vector<TrailEntry> trail_;
  std::optional<std::vector<sat::Lit>> conflict_;
  mutable IncrementalCongruenceClosureStats stats_;
  mutable std::vector<std::size_t> visit_marks_;
  mutable std::vector<std::size_t> reverse_visit_marks_;
  mutable std::vector<std::size_t> parent_edge_scratch_;
  mutable std::vector<std::size_t> reverse_parent_edge_scratch_;
  mutable std::vector<FlatTermId> parent_scratch_;
  mutable std::vector<FlatTermId> reverse_parent_scratch_;
  mutable std::vector<FlatTermId> queue_scratch_;
  mutable std::vector<FlatTermId> reverse_queue_scratch_;
  mutable std::vector<std::vector<std::size_t>> path_edges_scratch_;
  mutable std::unordered_map<ExplanationKey, std::size_t, ExplanationKeyHash> explanation_cache_;
  mutable std::size_t explanation_depth_ = 0;
  mutable std::size_t visit_generation_ = 0;
  mutable std::vector<std::size_t> reason_marks_;
  mutable std::size_t reason_generation_ = 0;
  std::vector<std::size_t> use_list_marks_;
  std::size_t use_list_generation_ = 0;
  FlatExplanationSearchMode explanation_search_mode_ = FlatExplanationSearchMode::OneSidedBfs;

  friend class FlatCongruenceClosureTestAccess;
};

}  // namespace primo::theory::equality
