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
  // from `e` to the root of e's own tree, then adds an edge e -> e'
  // labelled `label`. The caller chooses which of the two merged equation
  // endpoints plays `e` (conventionally the endpoint on the
  // smaller-by-ordinary-classlist-size side, matching the paper's Section
  // 5.1 "wlog" choice) versus `e_prime`; Union itself does not compare
  // sizes, since it operates on the independent proof-forest tree sizes,
  // not the congruence-closure classlist sizes. Precondition: `e` and
  // `e_prime` are not already connected in the proof forest (the paper's
  // "a' != b'" guard) -- Union does not check this.
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
  };

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

  // Tracks, per node, whether it has ever been used as `union_nodes`'s
  // `e_prime` (the union target). `parent_[node] == node` alone is not
  // enough to tell whether `node` is safe for the cheap alias fast path in
  // `coalesce_into_structural_class`: `union_nodes` never rewrites
  // `parent_[e_prime]` itself, so a node can already have a real incoming
  // tree edge from some earlier `e` while still looking self-rooted. Once
  // set, a node stays ineligible for the fast path even if reorientation
  // later moves its incoming edge elsewhere -- conservative, but the only
  // cost is an occasional unnecessary tree edge instead of a plain alias.
  std::vector<bool> has_incoming_edge_;
  std::vector<std::vector<FlatTermId>> incoming_edge_trail_;

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

  void aux_ensure_fresh(FlatTermId node) const;

  friend class ProofForestTestAccess;
};

}  // namespace primo::theory::equality
