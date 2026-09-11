#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "theory/theory_component.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::util {
class Statistics;
}

namespace primo::theory {

struct QuantifierInstantiationOptions {
  // Instances queued at one complete assignment, across all quantifiers.
  std::size_t instances_per_round = 128;
  // Instantiations tried in one solve before reporting ResourceLimit.
  std::size_t max_instances = 100000;
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
// set), with the max-then-lexicographic order on tuples. The subset and the
// deviations are listed in docs/papers/enumerative-instantiation.md. With no
// entailment check, running out of instances is `Unknown`, never `sat`.
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
    // Every tuple whose largest pool index is below this level was tried with
    // no pool clipped, so pool growth can add no tuple there.
    std::size_t first_open_level = 0;
  };

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

  std::size_t registered_atoms_ = 0;
  std::vector<AssignedTheoryLiteral> trail_;
  std::vector<std::size_t> levels_;

  std::unordered_map<core::SortId, std::vector<core::TermId>> pools_;
  std::unordered_set<core::TermId> walked_;
  std::set<std::pair<core::TermId, bool>> skolemized_;
  std::map<std::pair<core::TermId, bool>, Enumeration> enumerations_;
  std::deque<core::TermId> queue_;
  std::unordered_set<core::TermId> lemmas_;
  std::size_t instances_ = 0;
  std::size_t next_obligation_ = 0;
  std::uint64_t revision_ = 0;
};

}  // namespace primo::theory
