#include "theory/equality/flat_congruence_closure.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

namespace primo::theory::equality {

namespace {

void append_unique(std::vector<sat::Lit>& target, const std::vector<sat::Lit>& source) {
  for (const sat::Lit lit : source) {
    const auto duplicate = std::find_if(target.begin(), target.end(), [&](sat::Lit existing) {
      return existing.value == lit.value;
    });
    if (duplicate == target.end()) {
      target.push_back(lit);
    }
  }
}

void append_use(std::vector<FlatTermId>& use_list, FlatTermId result) {
  if (std::find(use_list.begin(), use_list.end(), result) == use_list.end()) {
    use_list.push_back(result);
  }
}

}  // namespace

std::size_t FlatCongruenceClosure::ApplyKeyHash::operator()(const ApplyKey& key) const {
  std::size_t seed = key.function + 0x9e3779b97f4a7c15ULL;
  seed ^= key.argument + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
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
  snapshots_.push_back(Snapshot{parent_, class_size_, class_members_, use_lists_, definitions_,
                                definition_by_result_, lookup_, pending_, disequalities_,
                                disequality_by_repr_, edges_, edge_adjacency_, conflict_, stats_});
  return snapshots_.size() - 1;
}

void FlatCongruenceClosure::pop(std::size_t checkpoint) {
  if (checkpoint >= snapshots_.size()) {
    throw std::out_of_range("flat congruence closure checkpoint out of range");
  }
  Snapshot snapshot = std::move(snapshots_[checkpoint]);
  snapshots_.resize(checkpoint);
  parent_ = std::move(snapshot.parent);
  class_size_ = std::move(snapshot.class_size);
  class_members_ = std::move(snapshot.class_members);
  use_lists_ = std::move(snapshot.use_lists);
  definitions_ = std::move(snapshot.definitions);
  definition_by_result_ = std::move(snapshot.definition_by_result);
  lookup_ = std::move(snapshot.lookup);
  pending_ = std::move(snapshot.pending);
  disequalities_ = std::move(snapshot.disequalities);
  disequality_by_repr_ = std::move(snapshot.disequality_by_repr);
  edges_ = std::move(snapshot.edges);
  edge_adjacency_ = std::move(snapshot.edge_adjacency);
  conflict_ = std::move(snapshot.conflict);
  stats_ = snapshot.stats;
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
  disequality_by_repr_.resize(node_count);
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
  definition_by_result_.emplace(definition.result, definitions_.size());
  definitions_.push_back(Definition{definition.function, definition.argument, definition.result});

  const Definition& stored = definitions_.back();
  const ApplyKey key = signature(stored);
  ++stats_.lookup_probes;
  if (const auto found = lookup_.find(key); found != lookup_.end()) {
    ++stats_.lookup_hits;
    enqueue(stored.result, found->second,
            congruence_reason(stored, definitions_[definition_by_result_.at(found->second)]), true);
    propagate();
    return;
  }
  lookup_.emplace(key, stored.result);
  append_use(use_lists_[representative(stored.function)], stored.result);
  append_use(use_lists_[representative(stored.argument)], stored.result);
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
  const std::size_t index = disequalities_.size();
  disequalities_.push_back(Disequality{lhs, rhs, std::move(reason)});
  const FlatTermId lhs_repr = representative(lhs);
  const FlatTermId rhs_repr = representative(rhs);
  watch_disequality(index, lhs_repr);
  if (rhs_repr != lhs_repr) {
    watch_disequality(index, rhs_repr);
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
  pending_.push_back(PendingMerge{lhs, rhs, std::move(reason), congruence});
}

void FlatCongruenceClosure::propagate() {
  while (!pending_.empty()) {
    PendingMerge pending = std::move(pending_.back());
    pending_.pop_back();
    ++stats_.pending_pops;
    if (representative(pending.lhs) == representative(pending.rhs)) {
      continue;
    }
    merge_classes(pending.lhs, pending.rhs, std::move(pending.reason), pending.congruence);
  }
}

void FlatCongruenceClosure::merge_classes(FlatTermId lhs, FlatTermId rhs,
                                          std::vector<sat::Lit> reason, bool congruence) {
  FlatTermId lhs_repr = representative(lhs);
  FlatTermId rhs_repr = representative(rhs);
  if (lhs_repr == rhs_repr) {
    return;
  }
  if (class_size_[lhs_repr] > class_size_[rhs_repr]) {
    std::swap(lhs_repr, rhs_repr);
  }

  const std::size_t edge_index = edges_.size();
  edges_.push_back(Edge{lhs, rhs, std::move(reason)});
  edge_adjacency_[lhs].push_back(edge_index);
  edge_adjacency_[rhs].push_back(edge_index);
  for (const FlatTermId member : class_members_[lhs_repr]) {
    parent_[member] = rhs_repr;
    class_members_[rhs_repr].push_back(member);
  }
  class_members_[lhs_repr].clear();
  class_size_[rhs_repr] += class_size_[lhs_repr];
  class_size_[lhs_repr] = 0;
  ++stats_.merges;
  if (congruence) {
    ++stats_.congruence_merges;
  }
  process_use_list(lhs_repr, rhs_repr);
  std::vector<std::size_t> watched = disequality_by_repr_[lhs_repr];
  move_disequality_watches(lhs_repr, rhs_repr);
  check_disequality_indices(watched);
}

void FlatCongruenceClosure::process_use_list(FlatTermId old_repr, FlatTermId new_repr) {
  std::vector<FlatTermId> affected;
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
        enqueue(result, found->second, congruence_reason(definition, other), true);
      }
      append_use(use_lists_[new_repr], result);
      continue;
    }
    lookup_.emplace(key, result);
    append_use(use_lists_[new_repr], result);
  }
}

void FlatCongruenceClosure::watch_disequality(std::size_t disequality_index, FlatTermId repr) {
  std::vector<std::size_t>& watches = disequality_by_repr_[repr];
  if (std::find(watches.begin(), watches.end(), disequality_index) == watches.end()) {
    watches.push_back(disequality_index);
  }
}

void FlatCongruenceClosure::move_disequality_watches(FlatTermId old_repr, FlatTermId new_repr) {
  if (old_repr == new_repr) {
    return;
  }
  for (const std::size_t disequality_index : disequality_by_repr_[old_repr]) {
    watch_disequality(disequality_index, new_repr);
  }
  disequality_by_repr_[old_repr].clear();
}

bool FlatCongruenceClosure::check_disequality(const Disequality& disequality) {
  if (conflict_.has_value()) {
    return true;
  }
  ++stats_.disequality_checks;
  if (representative(disequality.lhs) != representative(disequality.rhs)) {
    return false;
  }
  std::vector<sat::Lit> reason =
      explain_equal(disequality.lhs, disequality.rhs).value_or(std::vector<sat::Lit>{});
  append_unique(reason, disequality.reason);
  stats_.explain_length += reason.size();
  conflict_ = std::move(reason);
  ++stats_.conflicts;
  return true;
}

bool FlatCongruenceClosure::check_disequality_index(std::size_t disequality_index) {
  if (disequality_index >= disequalities_.size()) {
    throw std::out_of_range("flat congruence closure disequality index out of range");
  }
  return check_disequality(disequalities_[disequality_index]);
}

bool FlatCongruenceClosure::check_disequality_indices(
    const std::vector<std::size_t>& disequality_indices) {
  std::vector<std::size_t> ordered_indices = disequality_indices;
  std::sort(ordered_indices.begin(), ordered_indices.end());
  ordered_indices.erase(std::unique(ordered_indices.begin(), ordered_indices.end()),
                        ordered_indices.end());
  for (const std::size_t disequality_index : ordered_indices) {
    if (check_disequality_index(disequality_index)) {
      return true;
    }
  }
  return false;
}

void FlatCongruenceClosure::check_disequalities() {
  if (conflict_.has_value()) {
    return;
  }
  for (std::size_t i = 0; i < disequalities_.size(); ++i) {
    if (check_disequality_index(i)) {
      return;
    }
  }
}

std::optional<std::vector<sat::Lit>> FlatCongruenceClosure::explain_equal(FlatTermId lhs,
                                                                          FlatTermId rhs) const {
  if (lhs == rhs) {
    return std::vector<sat::Lit>{};
  }
  std::vector<bool> visited(parent_.size(), false);
  std::vector<std::size_t> parent_edge(parent_.size(), edges_.size());
  std::vector<FlatTermId> parent(parent_.size(), kInvalidFlatTermId);
  std::queue<FlatTermId> queue;
  visited[lhs] = true;
  queue.push(lhs);

  while (!queue.empty() && !visited[rhs]) {
    const FlatTermId current = queue.front();
    queue.pop();
    for (const std::size_t edge_index : edge_adjacency_[current]) {
      const Edge& edge = edges_[edge_index];
      FlatTermId next = kInvalidFlatTermId;
      if (edge.lhs == current) {
        next = edge.rhs;
      } else if (edge.rhs == current) {
        next = edge.lhs;
      }
      if (next == kInvalidFlatTermId || next >= visited.size() || visited[next]) {
        continue;
      }
      visited[next] = true;
      parent[next] = current;
      parent_edge[next] = edge_index;
      queue.push(next);
    }
  }

  if (!visited[rhs]) {
    return std::nullopt;
  }
  std::vector<sat::Lit> reason;
  for (FlatTermId current = rhs; current != lhs; current = parent[current]) {
    append_unique(reason, edges_[parent_edge[current]].reason);
  }
  return reason;
}

std::vector<sat::Lit> FlatCongruenceClosure::congruence_reason(const Definition& lhs,
                                                               const Definition& rhs) const {
  std::vector<sat::Lit> reason;
  if (auto function_reason = explain_equal(lhs.function, rhs.function);
      function_reason.has_value()) {
    append_unique(reason, function_reason.value());
  }
  if (auto argument_reason = explain_equal(lhs.argument, rhs.argument);
      argument_reason.has_value()) {
    append_unique(reason, argument_reason.value());
  }
  return reason;
}

}  // namespace primo::theory::equality
