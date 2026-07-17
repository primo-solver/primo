#include "theory/equality/incremental_congruence_closure.hpp"

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

void append_use(std::vector<core::TermId>& use_list, core::TermId term) {
  if (std::find(use_list.begin(), use_list.end(), term) == use_list.end()) {
    use_list.push_back(term);
  }
}

}  // namespace

std::size_t IncrementalCongruenceClosure::SignatureHash::operator()(
    const Signature& signature) const {
  std::size_t seed = static_cast<std::size_t>(signature.symbol) + 0x9e3779b97f4a7c15ULL;
  for (const core::TermId arg : signature.args) {
    seed ^= static_cast<std::size_t>(arg) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
  }
  return seed;
}

IncrementalCongruenceClosure::IncrementalCongruenceClosure(const core::TermManager& terms)
    : terms_(terms) {
  ensure_capacity();
}

std::size_t IncrementalCongruenceClosure::push() {
  snapshots_.push_back(Snapshot{parent_, class_size_, class_members_, use_lists_, lookup_, pending_,
                                disequalities_, edges_, terms_seen_, seen_, conflict_, stats_,
                                has_nonempty_reasons_});
  return snapshots_.size() - 1;
}

void IncrementalCongruenceClosure::pop(std::size_t checkpoint) {
  if (checkpoint >= snapshots_.size()) {
    throw std::out_of_range("incremental congruence closure checkpoint out of range");
  }
  Snapshot snapshot = std::move(snapshots_[checkpoint]);
  snapshots_.resize(checkpoint);
  parent_ = std::move(snapshot.parent);
  class_size_ = std::move(snapshot.class_size);
  class_members_ = std::move(snapshot.class_members);
  use_lists_ = std::move(snapshot.use_lists);
  lookup_ = std::move(snapshot.lookup);
  pending_ = std::move(snapshot.pending);
  disequalities_ = std::move(snapshot.disequalities);
  edges_ = std::move(snapshot.edges);
  terms_seen_ = std::move(snapshot.terms_seen);
  seen_ = std::move(snapshot.seen);
  conflict_ = std::move(snapshot.conflict);
  stats_ = snapshot.stats;
  has_nonempty_reasons_ = snapshot.has_nonempty_reasons;
  ++stats_.backtracks;
}

void IncrementalCongruenceClosure::add_term(core::TermId term) {
  terms_.get(term);
  ensure_capacity();
  if (term >= seen_.size()) {
    throw std::logic_error("incremental congruence closure term storage did not grow");
  }
  if (seen_[term]) {
    return;
  }
  seen_[term] = true;
  terms_seen_.push_back(term);
  ++stats_.terms;
  for (const core::TermId child : terms_.get(term).children) {
    add_term(child);
  }
  if (terms_.get(term).kind == core::TermKind::Application) {
    register_application(term);
  }
}

void IncrementalCongruenceClosure::assert_equal(core::TermId lhs, core::TermId rhs,
                                                std::vector<sat::Lit> reason) {
  add_term(lhs);
  add_term(rhs);
  has_nonempty_reasons_ = has_nonempty_reasons_ || !reason.empty();
  enqueue(lhs, rhs, std::move(reason), false);
  propagate();
}

void IncrementalCongruenceClosure::assert_distinct(core::TermId lhs, core::TermId rhs,
                                                   std::vector<sat::Lit> reason) {
  add_term(lhs);
  add_term(rhs);
  has_nonempty_reasons_ = has_nonempty_reasons_ || !reason.empty();
  disequalities_.push_back(Disequality{lhs, rhs, std::move(reason)});
  check_disequalities();
}

void IncrementalCongruenceClosure::saturate() { propagate(); }

bool IncrementalCongruenceClosure::are_equal(core::TermId lhs, core::TermId rhs) {
  add_term(lhs);
  add_term(rhs);
  propagate();
  return representative(lhs) == representative(rhs);
}

std::optional<std::vector<sat::Lit>> IncrementalCongruenceClosure::explain_equal(core::TermId lhs,
                                                                                 core::TermId rhs) {
  add_term(lhs);
  add_term(rhs);
  propagate();
  if (representative(lhs) != representative(rhs)) {
    return std::nullopt;
  }
  std::optional<std::vector<sat::Lit>> reason = explain_equal_without_propagation(lhs, rhs);
  if (reason.has_value()) {
    stats_.explain_length += reason->size();
  }
  return reason;
}

std::optional<std::vector<sat::Lit>> IncrementalCongruenceClosure::conflict() const {
  return conflict_;
}

void IncrementalCongruenceClosure::ensure_capacity() {
  const std::size_t size = terms_.size();
  const std::size_t old_size = parent_.size();
  if (old_size >= size) {
    return;
  }
  parent_.resize(size);
  class_size_.resize(size, 1);
  class_members_.resize(size);
  use_lists_.resize(size);
  seen_.resize(size, false);
  for (std::size_t i = old_size; i < size; ++i) {
    parent_[i] = static_cast<core::TermId>(i);
    class_size_[i] = 1;
    class_members_[i] = {static_cast<core::TermId>(i)};
  }
}

core::TermId IncrementalCongruenceClosure::representative(core::TermId term) const {
  if (term >= parent_.size()) {
    throw std::out_of_range("incremental congruence closure term out of range");
  }
  return parent_[term];
}

IncrementalCongruenceClosure::Signature IncrementalCongruenceClosure::signature(
    core::TermId term) const {
  const core::Term& node = terms_.get(term);
  Signature result;
  result.symbol = node.symbol;
  result.args.reserve(node.children.size());
  for (const core::TermId child : node.children) {
    result.args.push_back(representative(child));
  }
  return result;
}

void IncrementalCongruenceClosure::register_application(core::TermId term) {
  ++stats_.flat_definitions;
  const Signature key = signature(term);
  ++stats_.lookup_probes;
  const auto found = lookup_.find(key);
  if (found != lookup_.end()) {
    ++stats_.lookup_hits;
    if (found->second != term) {
      enqueue(
          term, found->second,
          has_nonempty_reasons_ ? congruence_reason(term, found->second) : std::vector<sat::Lit>{},
          true);
      propagate();
    }
    return;
  }
  lookup_.emplace(key, term);
  for (const core::TermId child : terms_.get(term).children) {
    append_use(use_lists_[representative(child)], term);
  }
}

void IncrementalCongruenceClosure::enqueue(core::TermId lhs, core::TermId rhs,
                                           std::vector<sat::Lit> reason, bool congruence) {
  pending_.push_back(PendingMerge{lhs, rhs, std::move(reason), congruence});
}

void IncrementalCongruenceClosure::propagate() {
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

void IncrementalCongruenceClosure::merge_classes(core::TermId lhs, core::TermId rhs,
                                                 std::vector<sat::Lit> reason, bool congruence) {
  core::TermId lhs_repr = representative(lhs);
  core::TermId rhs_repr = representative(rhs);
  if (lhs_repr == rhs_repr) {
    return;
  }
  if (class_size_[lhs_repr] > class_size_[rhs_repr]) {
    std::swap(lhs_repr, rhs_repr);
  }

  add_edge(lhs, rhs, std::move(reason));
  for (const core::TermId member : class_members_[lhs_repr]) {
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
  check_disequalities();
}

void IncrementalCongruenceClosure::process_use_list(core::TermId old_repr, core::TermId new_repr) {
  std::vector<core::TermId> affected;
  affected.swap(use_lists_[old_repr]);
  for (const core::TermId term : affected) {
    const Signature key = signature(term);
    ++stats_.lookup_probes;
    const auto found = lookup_.find(key);
    if (found != lookup_.end()) {
      ++stats_.lookup_hits;
      if (representative(found->second) != representative(term)) {
        enqueue(term, found->second,
                has_nonempty_reasons_ ? congruence_reason(term, found->second)
                                      : std::vector<sat::Lit>{},
                true);
      }
      append_use(use_lists_[new_repr], term);
      continue;
    }
    lookup_.emplace(key, term);
    append_use(use_lists_[new_repr], term);
  }
}

void IncrementalCongruenceClosure::add_edge(core::TermId lhs, core::TermId rhs,
                                            std::vector<sat::Lit> reason) {
  if (lhs == rhs) {
    return;
  }
  edges_.push_back(Edge{lhs, rhs, std::move(reason)});
}

void IncrementalCongruenceClosure::check_disequalities() {
  if (conflict_.has_value()) {
    return;
  }
  for (const Disequality& disequality : disequalities_) {
    ++stats_.disequality_checks;
    if (representative(disequality.lhs) != representative(disequality.rhs)) {
      continue;
    }
    std::optional<std::vector<sat::Lit>> equality_reason =
        explain_equal_without_propagation(disequality.lhs, disequality.rhs);
    std::vector<sat::Lit> reason = equality_reason.value_or(std::vector<sat::Lit>{});
    append_unique(reason, disequality.reason);
    stats_.explain_length += reason.size();
    conflict_ = std::move(reason);
    ++stats_.conflicts;
    return;
  }
}

std::optional<std::vector<sat::Lit>>
IncrementalCongruenceClosure::explain_equal_without_propagation(core::TermId lhs,
                                                                core::TermId rhs) const {
  if (lhs == rhs) {
    return std::vector<sat::Lit>{};
  }
  if (lhs >= seen_.size() || rhs >= seen_.size() || !seen_[lhs] || !seen_[rhs]) {
    return std::nullopt;
  }

  std::vector<bool> visited(seen_.size(), false);
  std::vector<std::size_t> parent_edge(seen_.size(), edges_.size());
  std::vector<core::TermId> parent(seen_.size(), core::kInvalidTermId);
  std::queue<core::TermId> queue;
  visited[lhs] = true;
  queue.push(lhs);

  while (!queue.empty() && !visited[rhs]) {
    const core::TermId current = queue.front();
    queue.pop();
    for (std::size_t edge_index = 0; edge_index < edges_.size(); ++edge_index) {
      const Edge& edge = edges_[edge_index];
      core::TermId next = core::kInvalidTermId;
      if (edge.lhs == current) {
        next = edge.rhs;
      } else if (edge.rhs == current) {
        next = edge.lhs;
      }
      if (next == core::kInvalidTermId || next >= visited.size() || visited[next]) {
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
  for (core::TermId current = rhs; current != lhs; current = parent[current]) {
    append_unique(reason, edges_[parent_edge[current]].reason);
  }
  return reason;
}

std::vector<sat::Lit> IncrementalCongruenceClosure::congruence_reason(core::TermId lhs,
                                                                      core::TermId rhs) const {
  const core::Term& lhs_term = terms_.get(lhs);
  const core::Term& rhs_term = terms_.get(rhs);
  std::vector<sat::Lit> reason;
  if (lhs_term.symbol != rhs_term.symbol || lhs_term.children.size() != rhs_term.children.size()) {
    return reason;
  }
  for (std::size_t i = 0; i < lhs_term.children.size(); ++i) {
    std::optional<std::vector<sat::Lit>> child_reason =
        explain_equal_without_propagation(lhs_term.children[i], rhs_term.children[i]);
    if (child_reason.has_value()) {
      append_unique(reason, child_reason.value());
    }
  }
  return reason;
}

}  // namespace primo::theory::equality
