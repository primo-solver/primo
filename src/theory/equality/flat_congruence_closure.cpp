#include "theory/equality/flat_congruence_closure.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace primo::theory::equality {

namespace {

std::size_t lit_index(sat::Lit lit) {
  const std::size_t var = static_cast<std::size_t>(lit.value < 0 ? -lit.value : lit.value);
  return (var << 1U) + (lit.value < 0 ? 1U : 0U);
}

}  // namespace

std::size_t FlatCongruenceClosure::ApplyKeyHash::operator()(const ApplyKey& key) const {
  std::size_t seed = key.function + 0x9e3779b97f4a7c15ULL;
  seed ^= key.argument + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  return seed;
}

std::size_t FlatCongruenceClosure::ExplanationKeyHash::operator()(const ExplanationKey& key) const {
  std::size_t seed = key.lhs + 0x9e3779b97f4a7c15ULL;
  seed ^= key.rhs + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  return seed;
}

FlatCongruenceClosure::FlatCongruenceClosure(std::size_t node_count) {
  definitions_.reserve(node_count);
  definition_by_result_.reserve(node_count);
  lookup_.reserve(node_count);
  pending_.reserve(node_count);
  disequalities_.reserve(node_count);
  edges_.reserve(node_count);
  ensure_node_count(node_count);
}

std::size_t FlatCongruenceClosure::push() {
  checkpoints_.push_back(Checkpoint{parent_.size(), definitions_.size(), pending_.size(),
                                    disequalities_.size(), edges_.size(), trail_.size(), conflict_,
                                    stats_});
  return checkpoints_.size() - 1;
}

void FlatCongruenceClosure::pop(std::size_t checkpoint) {
  if (checkpoint >= checkpoints_.size()) {
    throw std::out_of_range("flat congruence closure checkpoint out of range");
  }
  Checkpoint state = std::move(checkpoints_[checkpoint]);
  checkpoints_.resize(checkpoint);
  while (trail_.size() > state.trail_size) {
    restore_trail_entry(trail_.back());
    trail_.pop_back();
  }
  definitions_.resize(state.definitions_size);
  pending_.resize(state.pending_size);
  disequalities_.resize(state.disequalities_size);
  edges_.resize(state.edges_size);
  parent_.resize(state.node_count);
  class_size_.resize(state.node_count);
  class_members_.resize(state.node_count);
  use_lists_.resize(state.node_count);
  disequality_lists_.resize(state.node_count);
  edge_adjacency_.resize(state.node_count);
  conflict_ = std::move(state.conflict);
  stats_ = state.stats;
  ++stats_.backtracks;
}

void FlatCongruenceClosure::ensure_node_count(std::size_t node_count) {
  const std::size_t old_size = parent_.size();
  if (old_size >= node_count) {
    return;
  }
  stats_.terms += node_count - old_size;
  parent_.resize(node_count);
  class_size_.resize(node_count, 1);
  class_members_.resize(node_count);
  use_lists_.resize(node_count);
  disequality_lists_.resize(node_count);
  edge_adjacency_.resize(node_count);
  for (std::size_t i = old_size; i < node_count; ++i) {
    parent_[i] = i;
    class_size_[i] = 1;
    class_members_[i] = {i};
  }
}

void FlatCongruenceClosure::add_definition(const FlatDefinition& definition) {
  ensure_node_count(std::max({definition.function, definition.argument, definition.result}) + 1);
  ++stats_.flat_definitions;
  const auto inserted_definition =
      definition_by_result_.emplace(definition.result, definitions_.size());
  if (inserted_definition.second && !checkpoints_.empty()) {
    TrailEntry entry;
    entry.kind = TrailKind::DefinitionByResultInsertion;
    entry.index = definition.result;
    trail_.push_back(std::move(entry));
  }
  definitions_.push_back(Definition{definition.function, definition.argument, definition.result});

  const Definition& stored = definitions_.back();
  const ApplyKey key = signature(stored);
  ++stats_.lookup_probes;
  if (const auto found = lookup_.find(key); found != lookup_.end()) {
    ++stats_.lookup_hits;
    enqueue_congruence(stored, definitions_[definition_by_result_.at(found->second)]);
    propagate();
    return;
  }
  const auto inserted_lookup = lookup_.emplace(key, stored.result);
  if (inserted_lookup.second && !checkpoints_.empty()) {
    TrailEntry entry;
    entry.kind = TrailKind::LookupInsertion;
    entry.lookup_key = key;
    trail_.push_back(std::move(entry));
  }
  append_use_tracked(representative(stored.function), stored.result);
  append_use_tracked(representative(stored.argument), stored.result);
}

void FlatCongruenceClosure::assert_equal(FlatTermId lhs, FlatTermId rhs,
                                         std::vector<sat::Lit> reason) {
  ensure_node_count(std::max(lhs, rhs) + 1);
  enqueue(lhs, rhs, std::move(reason), false);
  propagate();
}

void FlatCongruenceClosure::assert_distinct(FlatTermId lhs, FlatTermId rhs,
                                            std::vector<sat::Lit> reason) {
  ensure_node_count(std::max(lhs, rhs) + 1);
  const std::size_t disequality_index = disequalities_.size();
  disequalities_.push_back(Disequality{lhs, rhs, std::move(reason)});
  const FlatTermId lhs_repr = representative(lhs);
  const FlatTermId rhs_repr = representative(rhs);
  if (!checkpoints_.empty()) {
    TrailEntry entry;
    entry.kind = TrailKind::DisequalityListPop;
    entry.index = lhs_repr;
    trail_.push_back(std::move(entry));
  }
  disequality_lists_[lhs_repr].push_back(disequality_index);
  if (lhs_repr != rhs_repr) {
    if (!checkpoints_.empty()) {
      TrailEntry entry;
      entry.kind = TrailKind::DisequalityListPop;
      entry.index = rhs_repr;
      trail_.push_back(std::move(entry));
    }
    disequality_lists_[rhs_repr].push_back(disequality_index);
  }
  check_disequality(disequalities_.back());
}

bool FlatCongruenceClosure::are_equal(FlatTermId lhs, FlatTermId rhs) {
  ensure_node_count(std::max(lhs, rhs) + 1);
  propagate();
  return representative(lhs) == representative(rhs);
}

FlatTermId FlatCongruenceClosure::representative(FlatTermId term) const {
  if (term >= parent_.size()) {
    throw std::out_of_range("flat congruence closure term out of range");
  }
  return parent_[term];
}

FlatCongruenceClosure::ApplyKey FlatCongruenceClosure::signature(
    const Definition& definition) const {
  return ApplyKey{representative(definition.function), representative(definition.argument)};
}

void FlatCongruenceClosure::enqueue(FlatTermId lhs, FlatTermId rhs, std::vector<sat::Lit> reason,
                                    bool congruence) {
  PendingMerge pending;
  pending.lhs = lhs;
  pending.rhs = rhs;
  pending.reason = std::move(reason);
  pending.congruence = congruence;
  pending_.push_back(std::move(pending));
}

void FlatCongruenceClosure::enqueue_congruence(const Definition& lhs, const Definition& rhs) {
  PendingMerge pending;
  pending.lhs = lhs.result;
  pending.rhs = rhs.result;
  pending.congruence = true;
  pending.lhs_definition = lhs;
  pending.rhs_definition = rhs;
  pending_.push_back(std::move(pending));
}

void FlatCongruenceClosure::propagate() {
  while (!pending_.empty()) {
    PendingMerge pending = std::move(pending_.back());
    pending_.pop_back();
    ++stats_.pending_pops;
    if (representative(pending.lhs) == representative(pending.rhs)) {
      continue;
    }
    merge_classes(std::move(pending));
  }
}

void FlatCongruenceClosure::merge_classes(FlatTermId lhs, FlatTermId rhs,
                                          std::vector<sat::Lit> reason, bool congruence) {
  PendingMerge pending;
  pending.lhs = lhs;
  pending.rhs = rhs;
  pending.reason = std::move(reason);
  pending.congruence = congruence;
  merge_classes(std::move(pending));
}

void FlatCongruenceClosure::merge_classes(PendingMerge pending) {
  FlatTermId lhs = pending.lhs;
  FlatTermId rhs = pending.rhs;
  FlatTermId lhs_repr = representative(lhs);
  FlatTermId rhs_repr = representative(rhs);
  if (lhs_repr == rhs_repr) {
    return;
  }
  if (class_size_[lhs_repr] > class_size_[rhs_repr]) {
    std::swap(lhs_repr, rhs_repr);
  }

  const std::size_t edge_index = edges_.size();
  edges_.push_back(Edge{lhs, rhs, std::move(pending.reason), pending.congruence,
                        pending.lhs_definition, pending.rhs_definition});
  if (!checkpoints_.empty()) {
    TrailEntry lhs_entry;
    lhs_entry.kind = TrailKind::EdgeAdjacencyPop;
    lhs_entry.index = lhs;
    trail_.push_back(std::move(lhs_entry));
  }
  edge_adjacency_[lhs].push_back(edge_index);
  if (!checkpoints_.empty()) {
    TrailEntry rhs_entry;
    rhs_entry.kind = TrailKind::EdgeAdjacencyPop;
    rhs_entry.index = rhs;
    trail_.push_back(std::move(rhs_entry));
  }
  edge_adjacency_[rhs].push_back(edge_index);
  for (const FlatTermId member : class_members_[lhs_repr]) {
    push_parent_change(member);
    parent_[member] = rhs_repr;
    if (!checkpoints_.empty()) {
      TrailEntry entry;
      entry.kind = TrailKind::ClassMembersPop;
      entry.index = rhs_repr;
      trail_.push_back(std::move(entry));
    }
    class_members_[rhs_repr].push_back(member);
  }
  if (!checkpoints_.empty()) {
    TrailEntry entry;
    entry.kind = TrailKind::ClassMembersRestore;
    entry.index = lhs_repr;
    entry.flat_vector = class_members_[lhs_repr];
    trail_.push_back(std::move(entry));
  }
  class_members_[lhs_repr].clear();
  push_class_size_change(rhs_repr);
  push_class_size_change(lhs_repr);
  class_size_[rhs_repr] += class_size_[lhs_repr];
  class_size_[lhs_repr] = 0;
  ++stats_.merges;
  if (pending.congruence) {
    ++stats_.congruence_merges;
  }
  process_use_list(lhs_repr, rhs_repr);
  check_disequalities_touching(lhs_repr, rhs_repr);
  std::vector<std::size_t> merged_disequalities;
  merged_disequalities.reserve(disequality_lists_[rhs_repr].size() +
                               disequality_lists_[lhs_repr].size());
  std::merge(disequality_lists_[rhs_repr].begin(), disequality_lists_[rhs_repr].end(),
             disequality_lists_[lhs_repr].begin(), disequality_lists_[lhs_repr].end(),
             std::back_inserter(merged_disequalities));
  merged_disequalities.erase(std::unique(merged_disequalities.begin(), merged_disequalities.end()),
                             merged_disequalities.end());
  if (!checkpoints_.empty()) {
    TrailEntry rhs_entry;
    rhs_entry.kind = TrailKind::DisequalityListRestore;
    rhs_entry.index = rhs_repr;
    rhs_entry.size_vector = disequality_lists_[rhs_repr];
    trail_.push_back(std::move(rhs_entry));
    TrailEntry lhs_entry;
    lhs_entry.kind = TrailKind::DisequalityListRestore;
    lhs_entry.index = lhs_repr;
    lhs_entry.size_vector = disequality_lists_[lhs_repr];
    trail_.push_back(std::move(lhs_entry));
  }
  disequality_lists_[rhs_repr] = std::move(merged_disequalities);
  disequality_lists_[lhs_repr].clear();
}

void FlatCongruenceClosure::process_use_list(FlatTermId old_repr, FlatTermId new_repr) {
  std::vector<FlatTermId> affected;
  if (!checkpoints_.empty()) {
    TrailEntry entry;
    entry.kind = TrailKind::UseListRestore;
    entry.index = old_repr;
    entry.flat_vector = use_lists_[old_repr];
    trail_.push_back(std::move(entry));
  }
  affected.swap(use_lists_[old_repr]);
  for (const FlatTermId result : affected) {
    const auto definition_it = definition_by_result_.find(result);
    if (definition_it == definition_by_result_.end()) {
      continue;
    }
    const Definition& definition = definitions_[definition_it->second];
    const ApplyKey key = signature(definition);
    ++stats_.lookup_probes;
    if (const auto found = lookup_.find(key); found != lookup_.end()) {
      ++stats_.lookup_hits;
      if (representative(found->second) != representative(result)) {
        const Definition& other = definitions_[definition_by_result_.at(found->second)];
        enqueue_congruence(definition, other);
      }
      append_use_tracked(new_repr, result);
      continue;
    }
    const auto inserted_lookup = lookup_.emplace(key, result);
    if (inserted_lookup.second && !checkpoints_.empty()) {
      TrailEntry entry;
      entry.kind = TrailKind::LookupInsertion;
      entry.lookup_key = key;
      trail_.push_back(std::move(entry));
    }
    append_use_tracked(new_repr, result);
  }
}

void FlatCongruenceClosure::append_use_tracked(FlatTermId representative, FlatTermId result) {
  std::vector<FlatTermId>& use_list = use_lists_[representative];
  if (std::find(use_list.begin(), use_list.end(), result) != use_list.end()) {
    return;
  }
  if (!checkpoints_.empty()) {
    TrailEntry entry;
    entry.kind = TrailKind::UseListPop;
    entry.index = representative;
    trail_.push_back(std::move(entry));
  }
  use_list.push_back(result);
}

void FlatCongruenceClosure::push_parent_change(FlatTermId term) {
  if (checkpoints_.empty()) {
    return;
  }
  TrailEntry entry;
  entry.kind = TrailKind::Parent;
  entry.index = term;
  entry.flat_value = parent_[term];
  trail_.push_back(std::move(entry));
}

void FlatCongruenceClosure::push_class_size_change(FlatTermId term) {
  if (checkpoints_.empty()) {
    return;
  }
  TrailEntry entry;
  entry.kind = TrailKind::ClassSize;
  entry.index = term;
  entry.size_value = class_size_[term];
  trail_.push_back(std::move(entry));
}

void FlatCongruenceClosure::restore_trail_entry(TrailEntry& entry) {
  switch (entry.kind) {
    case TrailKind::Parent:
      if (entry.index < parent_.size()) {
        parent_[entry.index] = entry.flat_value;
      }
      return;
    case TrailKind::ClassSize:
      if (entry.index < class_size_.size()) {
        class_size_[entry.index] = entry.size_value;
      }
      return;
    case TrailKind::ClassMembersRestore:
      class_members_[entry.index] = std::move(entry.flat_vector);
      return;
    case TrailKind::ClassMembersPop:
      class_members_[entry.index].pop_back();
      return;
    case TrailKind::UseListRestore:
      use_lists_[entry.index] = std::move(entry.flat_vector);
      return;
    case TrailKind::UseListPop:
      use_lists_[entry.index].pop_back();
      return;
    case TrailKind::DisequalityListRestore:
      disequality_lists_[entry.index] = std::move(entry.size_vector);
      return;
    case TrailKind::DisequalityListPop:
      disequality_lists_[entry.index].pop_back();
      return;
    case TrailKind::EdgeAdjacencyPop:
      edge_adjacency_[entry.index].pop_back();
      return;
    case TrailKind::LookupInsertion:
      lookup_.erase(entry.lookup_key);
      return;
    case TrailKind::DefinitionByResultInsertion:
      definition_by_result_.erase(static_cast<FlatTermId>(entry.index));
      return;
  }
}

bool FlatCongruenceClosure::check_disequality(const Disequality& disequality) {
  if (conflict_.has_value()) {
    return true;
  }
  ++stats_.disequality_checks;
  if (representative(disequality.lhs) != representative(disequality.rhs)) {
    return false;
  }
  std::vector<sat::Lit> reason;
  begin_reason_collection(reason);
  (void)explain_equal_into(disequality.lhs, disequality.rhs, reason);
  append_unique_reason(reason, disequality.reason);
  stats_.explain_length += reason.size();
  conflict_ = std::move(reason);
  ++stats_.conflicts;
  return true;
}

void FlatCongruenceClosure::check_disequalities_touching(FlatTermId lhs_repr, FlatTermId rhs_repr) {
  if (conflict_.has_value()) {
    return;
  }
  const std::vector<std::size_t>& lhs_candidates = disequality_lists_[lhs_repr];
  const std::vector<std::size_t>& rhs_candidates = disequality_lists_[rhs_repr];
  std::size_t lhs_index = 0;
  std::size_t rhs_index = 0;
  while (lhs_index < lhs_candidates.size() && rhs_index < rhs_candidates.size()) {
    if (lhs_candidates[lhs_index] < rhs_candidates[rhs_index]) {
      ++lhs_index;
      continue;
    }
    if (rhs_candidates[rhs_index] < lhs_candidates[lhs_index]) {
      ++rhs_index;
      continue;
    }
    const std::size_t disequality_index = lhs_candidates[lhs_index];
    ++lhs_index;
    ++rhs_index;
    if (check_disequality(disequalities_[disequality_index])) {
      return;
    }
  }
}

std::optional<std::vector<sat::Lit>> FlatCongruenceClosure::explain_equal(FlatTermId lhs,
                                                                          FlatTermId rhs) const {
  std::vector<sat::Lit> reason;
  begin_reason_collection(reason);
  if (!explain_equal_into(lhs, rhs, reason)) {
    return std::nullopt;
  }
  return reason;
}

void FlatCongruenceClosure::begin_reason_collection(std::vector<sat::Lit>& reason) const {
  reason.clear();
  explanation_cache_.clear();
  ++reason_generation_;
  if (reason_generation_ == 0) {
    std::fill(reason_marks_.begin(), reason_marks_.end(), 0);
    ++reason_generation_;
  }
}

void FlatCongruenceClosure::append_unique_reason(std::vector<sat::Lit>& target,
                                                 const std::vector<sat::Lit>& source) const {
  for (const sat::Lit lit : source) {
    const std::size_t index = lit_index(lit);
    if (index >= reason_marks_.size()) {
      reason_marks_.resize(index + 1, 0);
    }
    if (reason_marks_[index] == reason_generation_) {
      continue;
    }
    reason_marks_[index] = reason_generation_;
    target.push_back(lit);
  }
}

FlatCongruenceClosure::ExplanationKey FlatCongruenceClosure::explanation_key(FlatTermId lhs,
                                                                             FlatTermId rhs) const {
  if (rhs < lhs) {
    std::swap(lhs, rhs);
  }
  return ExplanationKey{lhs, rhs};
}

void FlatCongruenceClosure::append_edge_reason(const Edge& edge,
                                               std::vector<sat::Lit>& reason) const {
  if (edge.congruence) {
    (void)explain_equal_into(edge.lhs_definition.function, edge.rhs_definition.function, reason);
    (void)explain_equal_into(edge.lhs_definition.argument, edge.rhs_definition.argument, reason);
    return;
  }
  append_unique_reason(reason, edge.reason);
}

bool FlatCongruenceClosure::explain_equal_into(FlatTermId lhs, FlatTermId rhs,
                                               std::vector<sat::Lit>& reason) const {
  if (lhs == rhs) {
    return true;
  }
  if (lhs >= parent_.size() || rhs >= parent_.size()) {
    return false;
  }
  ++stats_.explain_calls;
  const ExplanationKey key = explanation_key(lhs, rhs);
  if (explanation_cache_.contains(key)) {
    ++stats_.explain_cache_hits;
    return true;
  }
  if (visit_marks_.size() < parent_.size()) {
    visit_marks_.resize(parent_.size(), 0);
    parent_edge_scratch_.resize(parent_.size(), edges_.size());
    parent_scratch_.resize(parent_.size(), kInvalidFlatTermId);
  }
  ++visit_generation_;
  if (visit_generation_ == 0) {
    std::fill(visit_marks_.begin(), visit_marks_.end(), 0);
    ++visit_generation_;
  }
  queue_scratch_.clear();
  std::size_t queue_index = 0;
  visit_marks_[lhs] = visit_generation_;
  queue_scratch_.push_back(lhs);

  while (queue_index < queue_scratch_.size() && visit_marks_[rhs] != visit_generation_) {
    const FlatTermId current = queue_scratch_[queue_index++];
    for (const std::size_t edge_index : edge_adjacency_[current]) {
      const Edge& edge = edges_[edge_index];
      FlatTermId next = kInvalidFlatTermId;
      if (edge.lhs == current) {
        next = edge.rhs;
      } else if (edge.rhs == current) {
        next = edge.lhs;
      }
      if (next == kInvalidFlatTermId || next >= visit_marks_.size() ||
          visit_marks_[next] == visit_generation_) {
        continue;
      }
      visit_marks_[next] = visit_generation_;
      parent_scratch_[next] = current;
      parent_edge_scratch_[next] = edge_index;
      queue_scratch_.push_back(next);
    }
  }

  if (visit_marks_[rhs] != visit_generation_) {
    return false;
  }
  if (path_edges_scratch_.size() <= explanation_depth_) {
    path_edges_scratch_.resize(explanation_depth_ + 1);
  }
  std::vector<std::size_t>& path_edges = path_edges_scratch_[explanation_depth_++];
  path_edges.clear();
  for (FlatTermId current = rhs; current != lhs; current = parent_scratch_[current]) {
    path_edges.push_back(parent_edge_scratch_[current]);
  }
  for (const std::size_t edge_index : path_edges) {
    append_edge_reason(edges_[edge_index], reason);
  }
  explanation_cache_.insert(key);
  --explanation_depth_;
  return true;
}

}  // namespace primo::theory::equality
