#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "sat/lit.hpp"
#include "theory/equality/congruence_stats.hpp"

namespace primo::theory::equality {

class IncrementalCongruenceClosure {
 public:
  explicit IncrementalCongruenceClosure(const core::TermManager& terms);

  std::size_t push();
  void pop(std::size_t checkpoint);

  void add_term(core::TermId term);
  void assert_equal(core::TermId lhs, core::TermId rhs, std::vector<sat::Lit> reason = {});
  void assert_distinct(core::TermId lhs, core::TermId rhs, std::vector<sat::Lit> reason = {});
  bool are_equal(core::TermId lhs, core::TermId rhs);
  std::optional<std::vector<sat::Lit>> explain_equal(core::TermId lhs, core::TermId rhs);
  std::optional<std::vector<sat::Lit>> conflict() const;
  const IncrementalCongruenceClosureStats& stats() const { return stats_; }

 private:
  struct Signature {
    core::SymbolId symbol = core::kInvalidSymbolId;
    std::vector<core::TermId> args;

    bool operator==(const Signature& other) const = default;
  };

  struct SignatureHash {
    std::size_t operator()(const Signature& signature) const;
  };

  struct PendingMerge {
    core::TermId lhs = core::kInvalidTermId;
    core::TermId rhs = core::kInvalidTermId;
    std::vector<sat::Lit> reason;
    bool congruence = false;
  };

  struct Disequality {
    core::TermId lhs = core::kInvalidTermId;
    core::TermId rhs = core::kInvalidTermId;
    std::vector<sat::Lit> reason;
  };

  struct Edge {
    core::TermId lhs = core::kInvalidTermId;
    core::TermId rhs = core::kInvalidTermId;
    std::vector<sat::Lit> reason;
  };

  struct Snapshot {
    std::vector<core::TermId> parent;
    std::vector<std::size_t> class_size;
    std::vector<std::vector<core::TermId>> class_members;
    std::vector<std::vector<core::TermId>> use_lists;
    std::unordered_map<Signature, core::TermId, SignatureHash> lookup;
    std::vector<PendingMerge> pending;
    std::vector<Disequality> disequalities;
    std::vector<Edge> edges;
    std::vector<core::TermId> terms_seen;
    std::vector<bool> seen;
    std::optional<std::vector<sat::Lit>> conflict;
    IncrementalCongruenceClosureStats stats;
  };

  void ensure_capacity();
  core::TermId representative(core::TermId term) const;
  Signature signature(core::TermId term) const;
  void register_application(core::TermId term);
  void enqueue(core::TermId lhs, core::TermId rhs, std::vector<sat::Lit> reason, bool congruence);
  void propagate();
  void merge_classes(core::TermId lhs, core::TermId rhs, std::vector<sat::Lit> reason,
                     bool congruence);
  void process_use_list(core::TermId old_repr, core::TermId new_repr);
  void add_edge(core::TermId lhs, core::TermId rhs, std::vector<sat::Lit> reason);
  void check_disequalities();
  std::optional<std::vector<sat::Lit>> explain_equal_without_propagation(core::TermId lhs,
                                                                         core::TermId rhs) const;
  std::vector<sat::Lit> congruence_reason(core::TermId lhs, core::TermId rhs) const;

  const core::TermManager& terms_;
  std::vector<core::TermId> parent_;
  std::vector<std::size_t> class_size_;
  std::vector<std::vector<core::TermId>> class_members_;
  std::vector<std::vector<core::TermId>> use_lists_;
  std::unordered_map<Signature, core::TermId, SignatureHash> lookup_;
  std::vector<PendingMerge> pending_;
  std::vector<Disequality> disequalities_;
  std::vector<Edge> edges_;
  std::vector<core::TermId> terms_seen_;
  std::vector<bool> seen_;
  std::vector<Snapshot> snapshots_;
  std::optional<std::vector<sat::Lit>> conflict_;
  IncrementalCongruenceClosureStats stats_;
};

}  // namespace primo::theory::equality
