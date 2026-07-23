#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "theory/equality/flat_terms.hpp"

namespace primo::theory::equality {

class ProofForestTestAccess;

// A congruence-step edge's two child obligations, to be recursively
// explained by the caller (the paper's PendingProofs additions).
struct ProofForestObligation {
  FlatTermId lhs = kInvalidFlatTermId;
  FlatTermId rhs = kInvalidFlatTermId;

  bool operator==(const ProofForestObligation& other) const = default;
};

// Exactly one of `reason` (a direct-equation leaf, an opaque id owned by the
// caller, e.g. an edge index) or `congruence` (a congruence-step pair of
// child obligations) is populated.
struct ProofForestLabel {
  std::optional<std::size_t> reason;
  std::optional<std::pair<ProofForestObligation, ProofForestObligation>> congruence;
};

// Nieuwenhuis and Oliveras, "Fast Congruence Closure and Extensions",
// Information and Computation 205 (2007). Implements Section 4 (union-find
// with Explain) applied to congruence closure per Section 5.1-5.4: a proof
// forest kept separate from the ordinary Representative/Find structure,
// reoriented at every union (Section 4's Union procedure: reverse the path
// from the smaller side to its own root, then link), explained via nearest
// common ancestor with an auxiliary union-find/HighestNode structure
// (Section 5.2) so that recursive congruence sub-explanations do not
// re-traverse subpaths already output in the current Explain call.
//
// Optionally also implements Section 5.5's structural-class refinement via
// `coalesce_into_structural_class`/`resolve`: a structural class is the set
// of congruence-produced terms sharing one (function, argument)
// representative pair (e.g. f(a1,a2)=a, f(b1,b2)=b, f(c1,c2)=c with
// a1~b1~c1 and a2~b2~c2 -- Theorem 21's definition). The paper coalesces
// every such term into a single proof-forest node instead of linking them
// by tree edges, since they are trivially equal by construction; this drops
// the observed explanation-redundancy rate from 13.92% to 3.3% on the
// paper's benchmark set (Section 5.5). Coalescing is opt-in per call
// (callers not using it get the plain Section 5.1-5.4 forest exactly as
// before). A node's own signature can be rehashed and rediscovered
// congruent more than once over its lifetime as unrelated merges change its
// arguments' representatives (see FlatCongruenceClosure's lookup-table
// congruence detection), so a node already coalesced once *can* later need
// to join a different class too. `coalesce_into_structural_class` only ever
// writes alias_[member] for a member with no existing alias and no real
// tree position (a genuinely fresh node); a member that already has either
// is first materialized/linked via the ordinary `union_nodes` (Section 4's
// Union, including its path-reversal handling of "e is not currently the
// root") rather than overwriting or bypassing what is already recorded --
// see that function's comment for why a single alias slot cannot safely
// carry two independent bridges.
class ProofForest {
 public:
  explicit ProofForest(std::size_t node_count = 0);

  std::size_t push();
  void pop(std::size_t checkpoint);
  void ensure_node_count(std::size_t node_count);

  // Section 5.5: coalesces `member` into `canonical`'s structural-class
  // node. `label` (always a `.congruence` pair here) justifies member =
  // canonical directly and is used by `explain` to bridge into the tree
  // when either endpoint is a non-canonical structural-class member. Safe
  // to call even if `member` was already coalesced elsewhere (see
  // `materialize`) or if `member`/`canonical` are equal or already
  // connected (a no-op in that case).
  void coalesce_into_structural_class(FlatTermId member, FlatTermId canonical,
                                      ProofForestLabel label);

  // Ensures `node` is a genuine, alias-free tree node: a no-op if it
  // already is one, otherwise converts its existing structural-class alias
  // into a real tree edge first. Required before using a raw (possibly
  // coalesced) node as a `union_nodes` endpoint -- resolving it to its
  // alias target instead and using that would silently drop the bridge
  // fact the alias represents from any explanation that later needs to
  // justify `node` itself, not just its resolved identity.
  void materialize(FlatTermId node);

  // Returns the proof-forest tree-identity node for `node`: `node` itself
  // if it has not been coalesced into a structural class, otherwise the
  // class's canonical member. Callers that mix direct-equation edges with
  // structural-class coalescing must resolve raw endpoints before calling
  // `union_nodes` (a direct equation's endpoint may itself be a
  // non-canonical structural-class member -- see the paper's Example 23).
  FlatTermId resolve(FlatTermId node) const;

  // Section 4 Union(e, e'): reverses every proof-forest edge on the path
  // from the smaller-by-real-tree-size of {e, e'} to the root of its own
  // tree, then adds an edge from it to the other one, labelled `label`.
  // The caller picks which argument is nominally `e` versus `e_prime`
  // (conventionally the endpoint on the smaller-by-ordinary-classlist-size
  // side, matching the paper's Section 5.1 "wlog" choice, e.g. for a plain
  // per-constant forest with no structural-class coalescing, where live
  // classlist size and real tree size always agree) but Union itself may
  // swap their roles internally if the *actual* tracked proof-forest tree
  // sizes disagree with that choice -- required for Lemma 13's amortized
  // reversal bound, and necessary once structural-class coalescing is in
  // play, since a class's live size and its real tree size can diverge
  // (see `size_`'s comment). This is why the label is not permitted to
  // depend on which physical node plays `e`: it must equally justify the
  // edge in either direction. Precondition: `e` and `e_prime` are not
  // already connected in the proof forest (the paper's "a' != b'" guard)
  // -- Union does not check this.
  void union_nodes(FlatTermId e, FlatTermId e_prime, ProofForestLabel label);

  // Section 5.2 Explain(c1, c2): recursively resolves congruence-step
  // obligations within this same forest (the paper's PendingProofs loop)
  // and returns the flattened list of leaf direct-equation reason ids.
  // Returns nullopt if c1 and c2 are not connected in the proof forest.
  std::optional<std::vector<std::size_t>> explain(FlatTermId c1, FlatTermId c2) const;

 private:
  struct PathEntry {
    FlatTermId node = kInvalidFlatTermId;
    FlatTermId old_parent = kInvalidFlatTermId;
    ProofForestLabel old_label;
  };

  struct AliasEntry {
    FlatTermId node = kInvalidFlatTermId;
    FlatTermId old_alias = kInvalidFlatTermId;
    ProofForestLabel old_label;
  };

  struct Checkpoint {
    std::size_t node_count = 0;
    std::size_t trail_size = 0;
    std::size_t alias_trail_size = 0;
    std::size_t incoming_edge_trail_size = 0;
    std::size_t size_trail_size = 0;
  };

  FlatTermId find_root(FlatTermId node) const;
  FlatTermId highest_node(FlatTermId node) const;
  FlatTermId aux_find(FlatTermId node) const;
  void aux_union_to(FlatTermId child, FlatTermId parent) const;
  std::optional<FlatTermId> nearest_common_ancestor(FlatTermId a, FlatTermId b) const;
  void explain_along_path(FlatTermId from, FlatTermId ancestor, std::vector<std::size_t>& reasons,
                          std::vector<ProofForestObligation>& pending_proofs) const;

  std::vector<FlatTermId> parent_;
  std::vector<ProofForestLabel> label_;
  std::vector<std::vector<PathEntry>> trail_;
  std::vector<Checkpoint> checkpoints_;

  // Section 5.5 structural-class alias layer: alias_[i] == i means `i` is
  // its own tree identity; otherwise `i` was coalesced into alias_[i], with
  // alias_label_[i] justifying the bridge. Always flat (see class comment).
  std::vector<FlatTermId> alias_;
  std::vector<ProofForestLabel> alias_label_;
  std::vector<std::vector<AliasEntry>> alias_trail_;

  // Tracks, per node, whether anything else already points *at* it -- as a
  // real `union_nodes` `e_prime` target, or as a `coalesce_into_structural_
  // class` fast-path alias target. `parent_[node] == node` alone is not
  // enough to tell whether `node` is safe for the cheap alias fast path:
  // `union_nodes` never rewrites `parent_[e_prime]` itself, so a node can
  // have a real incoming tree edge while still looking self-rooted, and an
  // alias target's `parent_` is untouched by definition. Either kind of
  // incoming pointer is a problem if `node` is later fast-aliased away
  // too: whoever points at it may eventually be `materialize`d into a real
  // tree edge landing on `node`, and the ordinary tree walk (`parent_`
  // only, never `alias_`, once past the obligation seeded at the start of
  // `explain`) would then dead-end at `node`'s self-loop instead of
  // continuing to wherever it got aliased. So both cases must be tracked
  // uniformly here. Once set, a node stays ineligible for the fast path
  // even if reorientation later moves its incoming edge elsewhere --
  // conservative, but the only cost is an occasional unnecessary tree edge
  // instead of a plain alias.
  std::vector<bool> has_incoming_edge_;
  std::vector<std::vector<FlatTermId>> incoming_edge_trail_;

  // Real proof-forest tree size, valid (and meaningful) only while `node`
  // is currently a root (parent_[node] == node); stale otherwise, since it
  // is never written when a node stops being a root, only read via a fresh
  // find_root() each time. Lemma 13's amortized O(log n) reversal bound
  // requires every Union to reverse the *smaller* side's path (so a given
  // edge's subtree size at least doubles between reversals) -- `union_nodes`
  // uses this to pick which of its two arguments actually plays Section 4's
  // `e` (reversed) versus `e'`, independent of which one the caller passed
  // as which. This is tracked here rather than reusing FlatCongruenceClosure's
  // live class_size_ (what the direct-equation caller uses to choose its
  // own initial e/e' before calling union_nodes) because that stops being
  // an accurate proxy for real tree size once structural-class fast-path
  // aliasing exists: an aliased member inflates its live union-find class
  // without adding any real tree depth at all.
  std::vector<std::size_t> size_;
  std::vector<std::vector<std::pair<FlatTermId, std::size_t>>> size_trail_;

  // Explain-local scratch: the auxiliary union-find (Section 5.2) and the
  // ancestor-search marks, logically reset at the start of every top-level
  // Explain call. Reset is generation-stamped (bump a counter, lazily
  // re-initialize each node the first time it is touched under the new
  // generation) rather than an eager O(n) array reset, since Explain is
  // called once per theory conflict/propagation and an eager reset would
  // dominate over the O(k) work the algorithm is supposed to cost. Mutable
  // because Explain is logically read-only over the forest.
  mutable std::vector<FlatTermId> aux_parent_;
  mutable std::vector<FlatTermId> aux_highest_;
  mutable std::vector<std::size_t> aux_generation_marks_;
  mutable std::size_t aux_generation_ = 0;
  mutable std::vector<std::size_t> ancestor_marks_;
  mutable std::size_t ancestor_generation_ = 0;

  // Same generation-stamp idea, but for `explain`'s alias-bridge seeding
  // (see `resolve`/`coalesce_into_structural_class`), which the aux
  // union-find above does not cover: that structure only deduplicates
  // repeat *tree*-edge congruence obligations reached via
  // `explain_along_path`'s NCA walk, not repeat *alias*-bridge obligations
  // reached via `seed_and_resolve`. Without this, a popular coalesced node
  // referenced by many different obligations within the same top-level
  // Explain call would have its bridge obligations re-pushed once per
  // reference instead of once per call.
  mutable std::vector<std::size_t> seed_marks_;
  mutable std::size_t seed_generation_ = 0;

  void aux_ensure_fresh(FlatTermId node) const;

  friend class ProofForestTestAccess;
};

}  // namespace primo::theory::equality
