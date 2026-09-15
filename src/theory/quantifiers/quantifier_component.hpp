#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "theory/quantifiers/instance_simplifier.hpp"
#include "theory/quantifiers/tuple_orders.hpp"
#include "theory/theory_component.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::util {
class Statistics;
}

namespace primo::theory {

// What instantiation used, recorded for checking an answer with another solver
// (smt/quantifiers/pool_export.hpp). Terms belong to the solve's TermManager.
struct QuantifierExport {
  // Constants the component created (Skolem and Herbrand), in creation order.
  std::vector<core::TermId> constants;
  // Skolem lemmas, as queued.
  std::vector<core::TermId> skolem_lemmas;
  // Tuples whose instance became a lemma, with their quantifier's variable sorts.
  std::vector<std::pair<std::vector<core::SortId>, std::vector<core::TermId>>> tuples;
};

struct QuantifierInstantiationOptions {
  // Instances queued at one complete assignment, across all quantifiers.
  std::size_t instances_per_round = kDefaultQuantifierInstancesPerRound;
  // Tuples one quantifier may visit without queuing a new lemma, once the round
  // has a lemma to deliver, before its enumeration yields to SAT; 0 never
  // yields. A yield skips no tuple and marks no stage complete.
  std::size_t stall_tuples = kDefaultQuantifierStallTuples;
  // Distinct instances generated in one solve before reporting ResourceLimit;
  // instances recognized as generated before do not count.
  std::size_t max_instances = 100000;
  // Normalize instances with InstanceSimplifier before the duplicate check and
  // before they become lemmas. Off, instances are compared as instantiated.
  bool simplify_instances = true;
  // Order of the candidate tuples for each quantifier (tuple_orders.hpp).
  TupleOrder tuple_order = kDefaultTupleOrder;
  // Maximum-depth increment of TupleOrder::IterativeDeepening.
  std::size_t deepening_increment = 2;
  // Seed of TupleOrder::RandomWalk, mixed with each quantifier's atom.
  std::uint64_t random_seed = 0;
  // Receives what instantiation used; null records nothing.
  QuantifierExport* export_sink = nullptr;
};

// Online component owning every Forall/Exists atom of the Boolean abstraction
// (docs/quantifiers.md, M4). It knows nothing about ground theories. At a
// complete assignment it queues, for the driver to clausify:
//
//   - one Skolem lemma per atom that behaves existentially (a true Exists or a
//     false Forall), with fresh constants: `not Q or body[c]` / `Q or not body[c]`;
//   - enumerative instances for atoms that behave universally (a true Forall or
//     a false Exists): `not Q or body[t]` / `Q or not body[t]`.
//
// Enumeration is strategy u of Reynolds, Barbosa and Fontaine, "Revisiting
// Enumerative Instantiation", TACAS 2018, section 4 and 4.1, over ground terms
// of the assertions and of every lemma already handed over (Theorem 1's term
// set). The tuple order is selectable (tuple_orders.hpp, after Janota, Barbosa,
// Fontaine and Reynolds, FMCAD 2021); the default is sum-of-indices order.
// The subset and the deviations are listed in
// docs/papers/enumerative-instantiation.md and
// docs/papers/fair-adventurous-enumeration.md. With no entailment check,
// running out of instances is `Unknown`, never `sat`.
class QuantifierComponent final : public TheoryComponent {
 public:
  using FreshConstantFactory = std::function<core::TermId(core::SortId, std::string_view)>;

  QuantifierComponent(core::TermManager& terms, std::span<const core::TermId> ground_roots,
                      FreshConstantFactory fresh_constant, QuantifierInstantiationOptions options,
                      util::Statistics& statistics);

  const ComponentDescriptor& descriptor() const override { return descriptor_; }
  bool owns_atom(core::TermId atom) const override;
  std::vector<core::TermId> interface_variables(core::TermId atom) const override;
  void register_interface(std::span<const ComponentInterfaceScope> scopes) override;
  void register_atoms(std::span<const ComponentAtom> atoms) override;
  core::TermId encode_equality(const InterfacePair& pair) override;
  void reset() override;
  void push_level() override;
  void pop_to_level(std::size_t level) override;
  void assign(std::span<const AssignedTheoryLiteral> delta) override;
  TheoryCheckResult check(TheoryCheckEffort effort, std::uint64_t assignment_epoch,
                          std::span<const InterfaceRelation> arrangement) override;
  std::vector<core::TermId> take_lemmas(std::size_t limit) override;
  ComponentModelFragment export_model(const TheoryModelToken& token) const override;

 private:
  // Enumeration state of one universally behaving (atom, value) obligation.
  struct Enumeration {
    std::vector<core::SortId> sorts;
    std::set<std::vector<core::TermId>> tried;
    // Staged orders: every stage below this one was enumerated with no tuple
    // outside the pools, so pool growth can add no tuple there.
    std::size_t first_open_level = 0;
    // Walk orders keep their traversal between rounds.
    std::unique_ptr<IterativeDeepening> deepening;
    std::unique_ptr<RandomWalk> random;
  };

  // One instance of an atom under one assignment value, the key of the caches
  // that recognize an instance generated before.
  struct InstanceKey {
    core::TermId atom = core::kInvalidTermId;
    bool value = false;
    core::TermId instance = core::kInvalidTermId;
    bool operator==(const InstanceKey&) const = default;
  };
  struct InstanceKeyHash {
    std::size_t operator()(const InstanceKey& key) const noexcept {
      std::size_t hash = std::hash<core::TermId>{}(key.atom);
      hash ^= std::hash<core::TermId>{}(key.instance) + 0x9e3779b97f4a7c15ULL + (hash << 6) +
              (hash >> 2);
      return hash ^ static_cast<std::size_t>(key.value);
    }
  };

  // Returns the canonical form of `term` when simplification is on.
  core::TermId canonical(core::TermId term);
  // Records and queues one enumerated instance; false when it adds nothing.
  bool add_instance(core::TermId atom, bool value, core::TermId instance);
  void add_pool_terms(core::TermId root);
  const std::vector<core::TermId>& pool(core::SortId sort);
  bool enqueue(core::TermId lemma);
  core::TermId lemma_for(core::TermId atom, bool value, core::TermId instance);
  void skolemize(core::TermId atom, bool value);
  std::size_t enumerate(core::TermId atom, bool value, std::size_t budget);

  core::TermManager& terms_;
  FreshConstantFactory fresh_constant_;
  QuantifierInstantiationOptions options_;
  util::Statistics& statistics_;
  ComponentDescriptor descriptor_;
  InstanceSimplifier simplifier_;

  std::size_t registered_atoms_ = 0;
  std::vector<AssignedTheoryLiteral> trail_;
  std::vector<std::size_t> levels_;

  std::unordered_map<core::SortId, std::vector<core::TermId>> pools_;
  std::unordered_set<core::TermId> walked_;
  std::set<std::pair<core::TermId, bool>> skolemized_;
  std::map<std::pair<core::TermId, bool>, Enumeration> enumerations_;
  std::deque<core::TermId> queue_;
  std::unordered_set<core::TermId> lemmas_;
  std::unordered_set<InstanceKey, InstanceKeyHash> raw_instances_;
  std::unordered_set<InstanceKey, InstanceKeyHash> canonical_instances_;
  std::size_t instances_ = 0;
  std::size_t next_obligation_ = 0;
  std::uint64_t revision_ = 0;
};

}  // namespace primo::theory
