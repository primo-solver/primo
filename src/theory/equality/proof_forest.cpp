#include "theory/equality/proof_forest.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace primo::theory::equality {

ProofForest::ProofForest(std::size_t node_count) { ensure_node_count(node_count); }

void ProofForest::ensure_node_count(std::size_t node_count) {
  const std::size_t old_size = parent_.size();
  if (old_size >= node_count) {
    return;
  }
  parent_.resize(node_count);
  label_.resize(node_count);
  alias_.resize(node_count);
  alias_label_.resize(node_count);
  has_incoming_edge_.resize(node_count);
  size_.resize(node_count);
  for (std::size_t i = old_size; i < node_count; ++i) {
    parent_[i] = i;
    alias_[i] = i;
    size_[i] = 1;
  }
}

std::size_t ProofForest::push() {
  checkpoints_.push_back(Checkpoint{parent_.size(), trail_.size(), alias_trail_.size(),
                                    incoming_edge_trail_.size(), size_trail_.size()});
  return checkpoints_.size() - 1;
}

void ProofForest::pop(std::size_t checkpoint) {
  if (checkpoint >= checkpoints_.size()) {
    throw std::out_of_range("proof forest checkpoint out of range");
  }
  const Checkpoint state = checkpoints_[checkpoint];
  checkpoints_.resize(checkpoint);
  while (trail_.size() > state.trail_size) {
    for (const PathEntry& entry : trail_.back()) {
      parent_[entry.node] = entry.old_parent;
      label_[entry.node] = entry.old_label;
    }
    trail_.pop_back();
  }
  while (alias_trail_.size() > state.alias_trail_size) {
    for (const AliasEntry& entry : alias_trail_.back()) {
      alias_[entry.node] = entry.old_alias;
      alias_label_[entry.node] = entry.old_label;
    }
    alias_trail_.pop_back();
  }
  while (incoming_edge_trail_.size() > state.incoming_edge_trail_size) {
    for (const FlatTermId node : incoming_edge_trail_.back()) {
      has_incoming_edge_[node] = false;
    }
    incoming_edge_trail_.pop_back();
  }
  while (size_trail_.size() > state.size_trail_size) {
    for (const auto& [node, old_size] : size_trail_.back()) {
      size_[node] = old_size;
    }
    size_trail_.pop_back();
  }
  parent_.resize(state.node_count);
  label_.resize(state.node_count);
  alias_.resize(state.node_count);
  alias_label_.resize(state.node_count);
  has_incoming_edge_.resize(state.node_count);
  size_.resize(state.node_count);
}

FlatTermId ProofForest::resolve(FlatTermId node) const {
  // Flat by construction (see class comment); loop defensively rather than
  // assuming, since a violated precondition should still terminate.
  while (alias_[node] != node) {
    node = alias_[node];
  }
  return node;
}

FlatTermId ProofForest::find_root(FlatTermId node) const {
  // Plain walk, no path compression: parent_ is read directly by `explain`
  // to reconstruct the actual proof path, so compressing it here would
  // corrupt the labels `explain_along_path` relies on. Compare `aux_find`,
  // which compresses a separate, Explain-call-local auxiliary structure.
  while (parent_[node] != node) {
    node = parent_[node];
  }
  return node;
}

void ProofForest::materialize(FlatTermId node) {
  ensure_node_count(node + 1);
  if (alias_[node] == node) {
    return;
  }
  // `node` is a structural-class member: converting it into a real tree
  // edge (exactly what a non-coalescing Union would have produced) makes it
  // usable as an ordinary Union endpoint without silently dropping the
  // bridge fact its alias represents -- resolving straight to its alias
  // target and using *that* instead would drop this bridge from any
  // explanation that later needs to justify `node` itself (see
  // coalesce_into_structural_class and the direct-equation caller in
  // FlatCongruenceClosure::merge_classes for the two ways this arises).
  const FlatTermId old_target = alias_[node];
  ProofForestLabel old_label = std::move(alias_label_[node]);
  AliasEntry entry;
  entry.node = node;
  entry.old_alias = old_target;
  entry.old_label = old_label;
  alias_trail_.push_back({entry});
  alias_[node] = node;
  alias_label_[node] = ProofForestLabel{};
  union_nodes(node, old_target, std::move(old_label));
}

void ProofForest::coalesce_into_structural_class(FlatTermId member, FlatTermId canonical,
                                                  ProofForestLabel label) {
  ensure_node_count(std::max(member, canonical) + 1);
  if (resolve(member) == resolve(canonical)) {
    return;
  }
  // `label` justifies member = canonical specifically (their own
  // definitions), so both must be linked *as themselves*, not as whatever
  // they currently resolve to -- resolving either one and linking through
  // the resolved id instead would drop that other one's own bridge fact
  // from the explanation (the same failure mode `materialize`'s comment
  // describes, and it applies to `canonical` here exactly as much as to
  // `member`: a canonical is only guaranteed alias-free at the moment it
  // is first recorded in FlatCongruenceClosure's lookup table, not forever
  // -- it can itself be rehashed into someone else's class later).
  materialize(canonical);
  materialize(member);
  if (parent_[member] == member && !has_incoming_edge_[member]) {
    // Still a genuinely fresh node -- untouched *and* nothing else's real
    // tree edge already points at it (see has_incoming_edge_'s comment) --
    // so the cheap alias path applies.
    AliasEntry entry;
    entry.node = member;
    entry.old_alias = alias_[member];
    entry.old_label = alias_label_[member];
    alias_trail_.push_back({entry});
    alias_[member] = canonical;
    alias_label_[member] = std::move(label);
    // `canonical` is now an alias *target*: if it is later itself coalesced
    // as someone else's member, the fast path above must not apply to it,
    // or `member`'s eventual materialize()-created real tree edge into
    // `canonical` would dead-end at a self-rooted parent_[canonical] the
    // ordinary tree walk cannot see past alias_ from (alias_ is only
    // consulted at the start of an obligation, not for nodes reached
    // mid-walk via parent_). has_incoming_edge_ must track alias-incoming
    // exactly like tree-incoming for that check to be sound.
    if (!has_incoming_edge_[canonical]) {
      has_incoming_edge_[canonical] = true;
      incoming_edge_trail_.push_back({canonical});
    }
    return;
  }
  // member already has real tree structure (either just materialized above,
  // or from an unrelated direct-equation edge touching it first) -- Section
  // 4's Union already handles "e is not the root of its own tree" via path
  // reversal, so this reconnects everything correctly.
  union_nodes(member, canonical, std::move(label));
}

void ProofForest::union_nodes(FlatTermId e, FlatTermId e_prime, ProofForestLabel label) {
  ensure_node_count(std::max(e, e_prime) + 1);

  // Lemma 13's amortized O(log n) bound on the number of times any single
  // edge gets reversed requires the reversed side to always be the smaller
  // one, so its subtree size at least doubles between reversals. The
  // caller's e/e' choice does not necessarily satisfy that once structural-
  // class coalescing is involved (see size_'s comment), so it is enforced
  // here regardless of which argument the caller passed as which: reverse
  // whichever side's *real* tree is smaller, swapping roles if needed. This
  // is sound because a tree edge's label proves its two endpoints equal
  // however it is attached; nothing about the label depends on which
  // physical node plays e versus e'.
  FlatTermId e_root = find_root(e);
  FlatTermId e_prime_root = find_root(e_prime);
  if (e_root != e_prime_root && size_[e_root] > size_[e_prime_root]) {
    std::swap(e, e_prime);
    std::swap(e_root, e_prime_root);
  }

  // Section 4, Union(e, e'): walk from e to the root of e's own tree,
  // recording each node's pre-mutation (parent, label) so the whole
  // operation -- reversal and the final link -- can be undone as one unit.
  std::vector<PathEntry> batch;
  FlatTermId node = e;
  while (true) {
    PathEntry entry;
    entry.node = node;
    entry.old_parent = parent_[node];
    entry.old_label = label_[node];
    batch.push_back(entry);
    if (parent_[node] == node) {
      break;
    }
    node = parent_[node];
  }

  // Reverse: edge v_i -> v_{i-1} takes the label the old edge v_{i-1} -> v_i
  // had, for i = size-1 down to 1. v_0 = e is left untouched here; it is
  // linked to e_prime below.
  for (std::size_t i = batch.size(); i-- > 1;) {
    parent_[batch[i].node] = batch[i - 1].node;
    label_[batch[i].node] = batch[i - 1].old_label;
  }

  parent_[e] = e_prime;
  label_[e] = std::move(label);
  trail_.push_back(std::move(batch));

  // e_root (the tree e used to be part of) is no longer a root, so its
  // size_ entry is left untouched -- stale until (if ever) a backtrack
  // makes it a root again, at which point it is already correct again,
  // since nothing here ever modifies it. Only the absorbing side's size,
  // e_prime_root (still the combined tree's root: e_prime's own path is
  // never touched above), needs updating and trailing.
  if (e_root != e_prime_root) {
    size_trail_.push_back({{e_prime_root, size_[e_prime_root]}});
    size_[e_prime_root] += size_[e_root];
  }

  // `parent_[e_prime]` itself is never touched above, so a self-rooted
  // e_prime would otherwise still look untouched to any later freshness
  // check even though it now has a real incoming edge from `e`.
  if (!has_incoming_edge_[e_prime]) {
    has_incoming_edge_[e_prime] = true;
    incoming_edge_trail_.push_back({e_prime});
  }
}

void ProofForest::aux_ensure_fresh(FlatTermId node) const {
  if (aux_generation_marks_[node] != aux_generation_) {
    aux_generation_marks_[node] = aux_generation_;
    aux_parent_[node] = node;
    aux_highest_[node] = node;
  }
}

FlatTermId ProofForest::aux_find(FlatTermId node) const {
  aux_ensure_fresh(node);
  FlatTermId root = node;
  while (true) {
    aux_ensure_fresh(root);
    if (aux_parent_[root] == root) {
      break;
    }
    root = aux_parent_[root];
  }
  while (node != root) {
    aux_ensure_fresh(node);
    const FlatTermId next = aux_parent_[node];
    aux_parent_[node] = root;
    node = next;
  }
  return root;
}

FlatTermId ProofForest::highest_node(FlatTermId node) const { return aux_highest_[aux_find(node)]; }

void ProofForest::aux_union_to(FlatTermId child, FlatTermId parent) const {
  // Paper (Section 5.2): "only unions of the form Union(a, parent(a)) will
  // take place, [so] the HighestNode of the new class is always the
  // HighestNode of the second argument."
  const FlatTermId child_root = aux_find(child);
  const FlatTermId parent_root = aux_find(parent);
  if (child_root == parent_root) {
    return;
  }
  const FlatTermId new_highest = aux_highest_[parent_root];
  aux_parent_[child_root] = parent_root;
  aux_highest_[parent_root] = new_highest;
}

std::optional<FlatTermId> ProofForest::nearest_common_ancestor(FlatTermId a, FlatTermId b) const {
  ++ancestor_generation_;
  if (ancestor_generation_ == 0) {
    std::fill(ancestor_marks_.begin(), ancestor_marks_.end(), 0);
    ++ancestor_generation_;
  }
  FlatTermId current = highest_node(a);
  while (true) {
    ancestor_marks_[current] = ancestor_generation_;
    if (parent_[current] == current) {
      break;
    }
    current = highest_node(parent_[current]);
  }
  current = highest_node(b);
  while (true) {
    if (ancestor_marks_[current] == ancestor_generation_) {
      return current;
    }
    if (parent_[current] == current) {
      break;
    }
    current = highest_node(parent_[current]);
  }
  if (ancestor_marks_[current] == ancestor_generation_) {
    return current;
  }
  return std::nullopt;
}

void ProofForest::explain_along_path(FlatTermId from, FlatTermId ancestor,
                                     std::vector<std::size_t>& reasons,
                                     std::vector<ProofForestObligation>& pending_proofs) const {
  FlatTermId a = highest_node(from);
  while (a != ancestor) {
    const FlatTermId b = parent_[a];
    const ProofForestLabel& label = label_[a];
    if (label.reason.has_value()) {
      reasons.push_back(*label.reason);
    } else {
      const auto& [first, second] = *label.congruence;
      pending_proofs.push_back(first);
      pending_proofs.push_back(second);
    }
    aux_union_to(a, b);
    a = highest_node(b);
  }
}

std::optional<std::vector<std::size_t>> ProofForest::explain(FlatTermId c1, FlatTermId c2) const {
  if (c1 >= parent_.size() || c2 >= parent_.size()) {
    return std::nullopt;
  }
  if (c1 == c2) {
    return std::vector<std::size_t>{};
  }
  // Section 5.2: "an additional union-find data structure is reset" at the
  // start of every top-level Explain call. Done lazily via a generation
  // stamp (grow-only resize plus a counter bump) rather than eagerly
  // reinitializing every node, since Explain is called once per theory
  // conflict/propagation and an eager O(n) reset would dominate the O(k)
  // work the algorithm is supposed to cost.
  if (aux_generation_marks_.size() < parent_.size()) {
    aux_parent_.resize(parent_.size());
    aux_highest_.resize(parent_.size());
    aux_generation_marks_.resize(parent_.size(), 0);
  }
  ++aux_generation_;
  if (aux_generation_ == 0) {
    std::fill(aux_generation_marks_.begin(), aux_generation_marks_.end(), 0);
    ++aux_generation_;
  }
  if (ancestor_marks_.size() < parent_.size()) {
    ancestor_marks_.resize(parent_.size(), 0);
  }
  // Same lazy generation-stamp reset for the alias-bridge seeding dedup
  // below (see seed_marks_'s comment).
  if (seed_marks_.size() < parent_.size()) {
    seed_marks_.resize(parent_.size(), 0);
  }
  ++seed_generation_;
  if (seed_generation_ == 0) {
    std::fill(seed_marks_.begin(), seed_marks_.end(), 0);
    ++seed_generation_;
  }

  std::vector<std::size_t> reasons;
  std::vector<ProofForestObligation> pending_proofs;

  // Section 5.5: every obligation processed by this loop -- not just the
  // top-level (c1, c2) -- can name a non-canonical structural-class member,
  // since a congruence-edge label's own obligations (from
  // explain_along_path) or a bridge label's obligations (from a coalesced
  // member further down an alias chain) are raw ids with no guarantee of
  // being tree-identity nodes. Bridge each side to its tree identity right
  // before use, seeding its own bridge obligations onto the same worklist
  // so a multi-hop alias chain (see coalesce_into_structural_class) is
  // resolved recursively rather than only one level deep. A node's bridge
  // obligations are only pushed the *first* time it is seeded within this
  // call (seed_marks_) -- otherwise a popular coalesced node referenced by
  // many different obligations in the same call would have its bridge
  // re-derived once per reference. The walk to find the resolved identity
  // still always runs regardless (cheap, and needed for correctness); only
  // the obligation-pushing is deduplicated.
  auto seed_and_resolve = [&](FlatTermId raw) {
    FlatTermId node = raw;
    while (alias_[node] != node) {
      if (seed_marks_[node] != seed_generation_) {
        seed_marks_[node] = seed_generation_;
        const auto& [first, second] = *alias_label_[node].congruence;
        pending_proofs.push_back(first);
        pending_proofs.push_back(second);
      }
      node = alias_[node];
    }
    return node;
  };
  pending_proofs.push_back(ProofForestObligation{c1, c2});
  while (!pending_proofs.empty()) {
    const ProofForestObligation obligation = pending_proofs.back();
    pending_proofs.pop_back();
    const FlatTermId lhs = seed_and_resolve(obligation.lhs);
    const FlatTermId rhs = seed_and_resolve(obligation.rhs);
    if (lhs == rhs) {
      continue;
    }
    const std::optional<FlatTermId> ancestor = nearest_common_ancestor(lhs, rhs);
    if (!ancestor.has_value()) {
      return std::nullopt;
    }
    explain_along_path(lhs, ancestor.value(), reasons, pending_proofs);
    explain_along_path(rhs, ancestor.value(), reasons, pending_proofs);
  }
  return reasons;
}

}  // namespace primo::theory::equality
