#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "sat/cadical_backend.hpp"
#include "smt/component_dispatcher.hpp"
#include "smt/component_model_coordinator.hpp"
#include "smt/theory_combination_plan.hpp"
#include "theory/theory_component.hpp"

namespace {

using namespace primo;
using Partition = std::vector<std::size_t>;

// Restricted-growth strings enumerate equivalence relations, not a fixed-size
// universe. Each allowed partition extends to an infinite carrier. This oracle
// does not use the coordinator's spanning edges or any Primo theory kernel.
std::vector<Partition> partitions(std::size_t size) {
  std::vector<Partition> result;
  Partition current(size, 0);
  const std::function<void(std::size_t, std::size_t)> enumerate = [&](std::size_t position,
                                                                      std::size_t maximum) {
    if (position == size) {
      result.push_back(current);
      return;
    }
    for (std::size_t value = 0; value <= maximum + 1; ++value) {
      current[position] = value;
      enumerate(position + 1, std::max(maximum, value));
    }
  };
  if (size == 0) {
    result.emplace_back();
  } else {
    enumerate(1, 0);
  }
  return result;
}

struct Restriction {
  std::vector<core::TermId> terms;
  std::vector<Partition> permitted;

  bool accepts(const std::map<core::TermId, std::size_t>& values) const {
    return std::any_of(permitted.begin(), permitted.end(), [&](const auto& pattern) {
      for (std::size_t lhs = 0; lhs < terms.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < terms.size(); ++rhs) {
          if ((values.at(terms[lhs]) == values.at(terms[rhs])) != (pattern[lhs] == pattern[rhs])) {
            return false;
          }
        }
      }
      return true;
    });
  }
};

class PartitionComponent final : public theory::TheoryComponent {
 public:
  PartitionComponent(core::TermManager& terms, std::string id, core::TermId marker,
                     std::vector<Restriction> restrictions)
      : terms_(terms), marker_(marker), restrictions_(std::move(restrictions)) {
    properties.id = std::move(id);
    properties.fragment = "independent finite boundary-partition oracle over infinite carriers";
    properties.capabilities.model_projection = true;
    properties.capabilities.model_export = true;
    properties.capabilities.incremental = true;
    properties.capabilities.interface_extension = true;
    for (const auto& restriction : restrictions_) {
      const auto sort = terms_.get(restriction.terms.front()).sort;
      if (std::none_of(properties.shared_sorts.begin(), properties.shared_sorts.end(),
                       [&](const auto& declared) { return declared.sort == sort; })) {
        properties.shared_sorts.push_back({sort, theory::StableInfiniteness::Yes, false});
      }
    }
  }
  const theory::ComponentDescriptor& descriptor() const override { return properties; }
  bool owns_atom(core::TermId atom) const override { return atom == marker_; }
  std::vector<core::TermId> interface_variables(core::TermId atom) const override {
    assert(atom == marker_);
    std::vector<core::TermId> result;
    for (const auto& restriction : restrictions_) {
      result.insert(result.end(), restriction.terms.begin(), restriction.terms.end());
    }
    return result;
  }
  void register_interface(std::span<const theory::ComponentInterfaceScope> scopes) override {
    scopes_.assign(scopes.begin(), scopes.end());
  }
  void register_atoms(std::span<const theory::ComponentAtom>) override {}
  core::TermId encode_equality(const theory::InterfacePair& pair) override {
    return terms_.mk_eq(pair.lhs, pair.rhs);
  }
  void reset() override {
    trail_.clear();
    levels_.clear();
  }
  void push_level() override { levels_.push_back(trail_.size()); }
  void pop_to_level(std::size_t level) override {
    assert(level <= levels_.size());
    if (level < levels_.size()) {
      trail_.resize(levels_[level]);
      levels_.resize(level);
    }
  }
  void assign(std::span<const theory::AssignedTheoryLiteral> delta) override {
    ++assignment_batches;
    trail_.insert(trail_.end(), delta.begin(), delta.end());
  }
  const std::vector<theory::AssignedTheoryLiteral>& assignments() const { return trail_; }
  std::optional<theory::TheoryCheckResult> propagate() override {
    return std::exchange(pending_propagation, std::nullopt);
  }
  theory::TheoryCheckResult check(theory::TheoryCheckEffort, std::uint64_t epoch,
                                  std::span<const theory::InterfaceRelation> arrangement) override {
    ++checks;
    theory::TheoryCheckResult result;
    result.status = theory::TheoryCheckStatus::Consistent;
    result.model.token = {properties.id, epoch, ++revision_};
    for (const auto& scope : scopes_) {
      bool found = false;
      for (const auto& partition : partitions(scope.terms.size())) {
        std::map<core::TermId, std::size_t> values;
        for (std::size_t index = 0; index < scope.terms.size(); ++index) {
          values.emplace(scope.terms[index], partition[index]);
        }
        bool allowed =
            std::all_of(restrictions_.begin(), restrictions_.end(), [&](const auto& rule) {
              return terms_.get(rule.terms.front()).sort != scope.sort || rule.accepts(values);
            });
        for (const auto& relation : arrangement) {
          if (relation.sort == scope.sort) {
            allowed = allowed && ((values.at(relation.lhs) == values.at(relation.rhs)) ==
                                  (relation.value == theory::InterfaceRelationValue::Equal));
          }
        }
        if (!allowed) {
          continue;
        }
        theory::SharedModelPartition projected;
        projected.sort = scope.sort;
        for (std::size_t index = 0; index < partition.size(); ++index) {
          projected.equality_classes.resize(
              std::max(projected.equality_classes.size(), partition[index] + 1));
          projected.equality_classes[partition[index]].terms.push_back(scope.terms[index]);
        }
        result.model.partitions.push_back(std::move(projected));
        found = true;
        break;
      }
      if (!found) {
        result.status = theory::TheoryCheckStatus::LearnedClause;
        for (const auto& assigned : trail_) {
          result.clause.push_back(
              sat::Lit{assigned.value ? -assigned.lit.value : assigned.lit.value});
        }
        for (const auto& relation : arrangement) {
          result.clause.push_back(sat::Lit{relation.value == theory::InterfaceRelationValue::Equal
                                               ? -relation.equality_lit.value
                                               : relation.equality_lit.value});
        }
        return result;
      }
    }
    if (corrupt) {
      corrupt(result);
    }
    last_model_ = result.model;
    return result;
  }
  theory::TheoryMutationResult mutate(const theory::TheoryMutationRequest& request) override {
    ++mutations;
    return mutation ? mutation(request) : theory::TheoryMutationResult{};
  }
  std::vector<theory::InterfacePair> care_pairs(const theory::TheoryModelToken&) const override {
    ++care_calls;
    return {};
  }
  theory::ComponentModelFragment export_model(
      const theory::TheoryModelToken& token) const override {
    assert(token == last_model_.token);
    return {token, {}, {}, {}};
  }

  theory::ComponentDescriptor properties;
  std::function<void(theory::TheoryCheckResult&)> corrupt;
  std::function<theory::TheoryMutationResult(const theory::TheoryMutationRequest&)> mutation;
  std::size_t checks = 0;
  std::size_t assignment_batches = 0;
  std::size_t mutations = 0;
  mutable std::size_t care_calls = 0;
  std::optional<theory::TheoryCheckResult> pending_propagation;

 private:
  core::TermManager& terms_;
  core::TermId marker_;
  std::vector<Restriction> restrictions_;
  std::vector<theory::ComponentInterfaceScope> scopes_;
  std::vector<theory::AssignedTheoryLiteral> trail_;
  std::vector<std::size_t> levels_;
  std::uint64_t revision_ = 0;
  theory::TheoryModelProjection last_model_;
};

struct Fixture {
  core::SymbolTable symbols;
  core::TermManager terms{symbols};
  std::vector<std::unique_ptr<PartitionComponent>> owned;
  std::vector<theory::TheoryComponent*> components;
  std::vector<theory::ComponentAtom> atoms;

  core::TermId variable(const std::string& name, core::SortId sort) {
    return terms.mk_constant(symbols.declare_constant(name, sort));
  }
  PartitionComponent& add(std::vector<Restriction> rules) {
    const auto id = "component" + std::to_string(owned.size());
    const auto marker = variable(id, symbols.bool_sort());
    owned.push_back(std::make_unique<PartitionComponent>(terms, id, marker, std::move(rules)));
    components.push_back(owned.back().get());
    atoms.push_back({marker, sat::Lit{static_cast<int>(atoms.size() + 1)}});
    return *owned.back();
  }
};

template <typename Action>
void requires_unsupported(Action action) {
  bool rejected = false;
  try {
    action();
  } catch (const smt::UnsupportedCombination&) {
    rejected = true;
  }
  assert(rejected);
}

void admission_and_normalization() {
  Fixture f;
  const auto sort = f.symbols.int_sort();
  const auto x = f.variable("x", sort);
  const auto y = f.variable("y", sort);
  const auto z = f.variable("z", sort);
  const auto& any_pair = partitions(2);
  f.add({{{x, y}, any_pair}});
  f.add({{{y, z}, any_pair}});
  f.add({{{x, z}, any_pair}});
  smt::TheoryCombinationPlan plan(f.terms, f.components, f.atoms, true);
  assert(plan.scopes().size() == 1);
  assert(plan.scopes()[0].terms == std::vector<core::TermId>({x, y, z}));
  for (std::size_t index = 0; index < 3; ++index) {
    assert(plan.component_scopes(index)[0].terms == plan.scopes()[0].terms);
    assert(plan.owner(f.atoms[index].term) == index);
  }
  assert(!plan.convex_complete_propagation());
  assert(plan.scope_for({z, x, sort}) == 0);
  assert(!plan.scope_for({x, y, f.symbols.real_sort()}).has_value());
  f.owned[1]->properties.shared_sorts[0].stable_infiniteness = theory::StableInfiniteness::No;
  requires_unsupported(
      [&] { smt::TheoryCombinationPlan bad(f.terms, f.components, f.atoms, true); });
  f.owned[1]->properties.shared_sorts[0].stable_infiniteness = theory::StableInfiniteness::Yes;
  f.owned[1]->properties.capabilities.interface_extension = false;
  requires_unsupported(
      [&] { smt::TheoryCombinationPlan bad(f.terms, f.components, f.atoms, true); });
  f.owned[1]->properties.capabilities.interface_extension = true;
  f.owned[1]->properties.capabilities.model_projection = false;
  smt::TheoryCombinationPlan complete(f.terms, f.components, f.atoms, true);
  assert(complete.policy() == smt::CombinationPolicy::CompleteArrangement);
  f.owned[0]->properties.signature_families = {"test-signature"};
  f.owned[1]->properties.signature_families = {"test-signature"};
  requires_unsupported(
      [&] { smt::TheoryCombinationPlan bad(f.terms, f.components, f.atoms, true); });
}

void sort_nonparticipants_and_disconnected_groups() {
  Fixture f;
  const auto int_sort = f.symbols.int_sort();
  const auto real_sort = f.symbols.real_sort();
  const auto x = f.variable("x", int_sort);
  const auto y = f.variable("y", int_sort);
  const auto u = f.variable("u", real_sort);
  const auto v = f.variable("v", real_sort);
  f.add({{{x, y}, {{0, 0}}}});
  f.add({{{x, y}, {{0, 0}}}, {{u, v}, {{0, 0}}}});
  f.add({{{u, v}, {{0, 0}}}});
  smt::TheoryCombinationPlan plan(f.terms, f.components, f.atoms, true);
  plan.register_interfaces(f.components);
  smt::ComponentModelCoordinator coordinator;
  std::size_t decisions = 0;
  const auto result = coordinator.reconcile(plan, 7, f.components, {}, [&](const auto&) {
    ++decisions;
    return sat::Lit{42};
  });
  assert(result.status == smt::ModelBasedCombinationStatus::Accept);
  assert(decisions == 0);
  assert(plan.component_scopes(0).size() == 1);
  assert(plan.component_scopes(1).size() == 2);
  assert(plan.component_scopes(2).size() == 1);
  const auto a = f.variable("a", int_sort);
  const auto b = f.variable("b", int_sort);
  f.add({{{a, b}, {{0, 1}}}});
  f.add({{{a, b}, {{0, 1}}}});
  smt::TheoryCombinationPlan disconnected(f.terms, f.components, f.atoms, true);
  assert(disconnected.scopes().size() == 3);
  assert(!disconnected.scope_for({x, a, int_sort}).has_value());
}

bool solve_with_sat(Fixture& f, bool guided) {
  smt::TheoryCombinationPlan plan(f.terms, f.components, f.atoms, guided);
  plan.register_interfaces(f.components);
  sat::CadicalBackend backend;
  for (std::size_t index = 0; index < f.atoms.size(); ++index) {
    assert(backend.new_var() == f.atoms[index].lit.value);
    backend.add_clause({f.atoms[index].lit});
    const std::vector delta{
        theory::AssignedTheoryLiteral{f.atoms[index].term, f.atoms[index].lit, true}};
    f.components[plan.owner(f.atoms[index].term)]->reset();
    f.components[plan.owner(f.atoms[index].term)]->assign(delta);
  }
  std::map<std::pair<core::TermId, core::TermId>, theory::InterfaceRelation> registered;
  const auto materialize = [&](const theory::InterfacePair& pair) {
    const auto key = std::minmax(pair.lhs, pair.rhs);
    const auto found = registered.find(key);
    if (found != registered.end()) {
      return found->second.equality_lit;
    }
    const sat::Lit literal{backend.new_var()};
    backend.add_clause({literal, sat::Lit{-literal.value}});
    registered.emplace(key, theory::InterfaceRelation{pair.lhs, pair.rhs, pair.sort, literal,
                                                      theory::InterfaceRelationValue::Equal});
    return literal;
  };
  if (!guided) {
    for (const auto& scope : plan.scopes()) {
      for (std::size_t lhs = 0; lhs < scope.terms.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < scope.terms.size(); ++rhs) {
          materialize({scope.terms[lhs], scope.terms[rhs], scope.sort});
        }
      }
    }
  }
  smt::ComponentModelCoordinator coordinator;
  for (std::uint64_t epoch = 1; epoch < 100; ++epoch) {
    const auto sat_result = backend.solve();
    if (sat_result == sat::SatResult::Unsat) {
      return false;
    }
    assert(sat_result == sat::SatResult::Sat);
    std::vector<theory::InterfaceRelation> arrangement;
    for (const auto& [key, relation] : registered) {
      static_cast<void>(key);
      auto assigned = relation;
      const auto value = backend.value(relation.equality_lit.value);
      assert(value.has_value());
      assigned.value =
          *value ? theory::InterfaceRelationValue::Equal : theory::InterfaceRelationValue::Disequal;
      arrangement.push_back(assigned);
    }
    const auto result = coordinator.reconcile(plan, epoch, f.components, arrangement, materialize);
    switch (result.status) {
      case smt::ModelBasedCombinationStatus::Accept:
        return true;
      case smt::ModelBasedCombinationStatus::LearnClause:
        backend.add_clause(result.clause);
        break;
      case smt::ModelBasedCombinationStatus::DecideEquality:
        for (const auto literal : result.decision_lits) {
          backend.phase(literal);
        }
        break;
      default:
        assert(false && "unexpected coordinator failure");
    }
  }
  assert(false && "combination did not terminate");
  return false;
}

void exhaustive_three_and_four_component_sat() {
  for (const bool guided : {false, true}) {
    for (const std::size_t count : {3U, 4U}) {
      // Every combination of equality-only, disequality-only and unrestricted
      // binary components on a cycle, in both recipe directions.
      std::size_t cases = 1;
      for (std::size_t index = 0; index < count; ++index) {
        cases *= 3;
      }
      for (std::size_t encoded = 0; encoded < cases; ++encoded) {
        Fixture f;
        std::vector<core::TermId> variables;
        for (std::size_t index = 0; index < count; ++index) {
          variables.push_back(f.variable("x" + std::to_string(index), f.symbols.int_sort()));
        }
        std::vector<Restriction> restrictions;
        auto cursor = encoded;
        for (std::size_t index = 0; index < count; ++index) {
          const auto choice = cursor % 3;
          cursor /= 3;
          Restriction rule{{variables[index], variables[(index + 1) % count]}, {}};
          if (choice != 1) {
            rule.permitted.push_back({0, 0});
          }
          if (choice != 0) {
            rule.permitted.push_back({0, 1});
          }
          restrictions.push_back(rule);
          f.add({rule});
        }
        bool expected = false;
        for (const auto& partition : partitions(count)) {
          std::map<core::TermId, std::size_t> values;
          for (std::size_t index = 0; index < count; ++index) {
            values.emplace(variables[index], partition[index]);
          }
          expected =
              expected || std::all_of(restrictions.begin(), restrictions.end(),
                                      [&](const auto& rule) { return rule.accepts(values); });
        }
        assert(solve_with_sat(f, guided) == expected);
        std::reverse(f.components.begin(), f.components.end());
        assert(solve_with_sat(f, guided) == expected);
      }
    }
  }
}

void malformed_models_are_not_accepted_or_redecided() {
  Fixture f;
  const auto sort = f.symbols.int_sort();
  const auto x = f.variable("x", sort);
  const auto y = f.variable("y", sort);
  f.add({{{x, y}, {{0, 0}}}});
  f.add({{{x, y}, {{0, 0}}}});
  smt::TheoryCombinationPlan plan(f.terms, f.components, f.atoms, true);
  plan.register_interfaces(f.components);
  smt::ComponentModelCoordinator coordinator;
  const auto resolver = [](const auto&) { return sat::Lit{42}; };
  for (const int fault : {0, 1, 2}) {
    f.owned[1]->corrupt = [fault](auto& checked) {
      if (fault == 0) {
        --checked.model.token.assignment_epoch;
      } else if (fault == 1) {
        checked.model.partitions[0].equality_classes[0].terms.pop_back();
      } else {
        checked.model.token.component = "wrong-component";
      }
    };
    assert(coordinator.reconcile(plan, 7, f.components, {}, resolver).status ==
           smt::ModelBasedCombinationStatus::Unknown);
  }
  f.owned[1]->corrupt = [](auto& checked) {
    checked.status = theory::TheoryCheckStatus::FreshDecision;
    const auto& projection = checked.model.partitions[0];
    const auto& terms = projection.equality_classes[0].terms;
    checked.decision.pair = {terms[0], terms[1], projection.sort};
  };
  const std::vector arrangement{
      theory::InterfaceRelation{x, y, sort, sat::Lit{42}, theory::InterfaceRelationValue::Equal}};
  assert(coordinator.reconcile(plan, 7, f.components, arrangement, resolver).status ==
         smt::ModelBasedCombinationStatus::Unknown);
  assert(f.owned[0]->mutations == 0 && f.owned[1]->mutations == 0);
  assert(f.owned[0]->care_calls == 0 && f.owned[1]->care_calls == 0);
}

void incremental_routing_backtracking_and_explanation_lifetime() {
  Fixture f;
  const auto x = f.variable("x", f.symbols.int_sort());
  const auto y = f.variable("y", f.symbols.int_sort());
  for (std::size_t index = 0; index < 4; ++index) {
    f.add({{{x, y}, partitions(2)}});
  }
  smt::ComponentDispatcher dispatcher(f.components);
  for (std::size_t index = 0; index < f.atoms.size(); ++index) {
    dispatcher.register_atoms(index, std::span(&f.atoms[index], 1));
  }
  dispatcher.notify_assignment({1, 3, 99, 1});
  assert(f.owned[0]->assignments().size() == 1);
  assert(f.owned[1]->assignments().empty());
  assert(f.owned[2]->assignments().size() == 1);
  assert(f.owned[3]->assignments().empty());
  assert(dispatcher.statistics().routed_literals == 2);
  assert(dispatcher.statistics().assignment_batches == 2);
  assert(!dispatcher.cb_has_external_clause());
  assert(f.owned[0]->checks == 1 && f.owned[2]->checks == 1);
  assert(!dispatcher.cb_has_external_clause());
  assert(f.owned[0]->checks == 1 && f.owned[2]->checks == 1);

  theory::TheoryCheckResult consequence;
  consequence.status = theory::TheoryCheckStatus::EntailedLiteral;
  consequence.entailed_lit = sat::Lit{2};
  consequence.reasons = {sat::Lit{1}};
  f.owned[1]->pending_propagation = consequence;
  assert(dispatcher.cb_propagate() == 2);
  dispatcher.notify_assignment({2});
  dispatcher.notify_new_decision_level();
  dispatcher.notify_assignment({-4});
  dispatcher.notify_new_decision_level();
  dispatcher.notify_backtrack(0);
  assert(f.owned[3]->assignments().empty());
  assert(f.owned[1]->assignments().size() == 1);
  // The root propagation's explanation survives unrelated non-root undo.
  assert(dispatcher.cb_add_reason_clause_lit(2) == 2);
  assert(dispatcher.cb_add_reason_clause_lit(2) == -1);
  assert(dispatcher.cb_add_reason_clause_lit(2) == 0);
  dispatcher.notify_new_decision_level();
  dispatcher.notify_assignment({4});
  assert(f.owned[3]->assignments().size() == 1);
  assert(f.owned[3]->assignments()[0].value);
  dispatcher.notify_backtrack(0);

  f.owned[0]->corrupt = [](auto& result) {
    result.status = theory::TheoryCheckStatus::Cancelled;
    result.diagnostic = "test cancellation";
  };
  assert(!dispatcher.cb_has_external_clause());
  assert(dispatcher.failure()->status == theory::TheoryCheckStatus::Cancelled);
  assert(dispatcher.cb_propagate() == 0);
}

void nonconvex_disjunction_and_mutation_side_effects() {
  for (const bool guided : {false, true}) {
    for (const bool contradiction : {false, true}) {
      Fixture f;
      const auto sort = f.symbols.int_sort();
      const auto x = f.variable("x", sort);
      const auto y = f.variable("y", sort);
      const auto z = f.variable("z", sort);
      // Exactly (x=y or y=z) and x!=z: neither equality alone is entailed.
      f.add({{{x, y, z}, {{0, 0, 1}, {0, 1, 1}}}});
      f.add({{{x, y}, {{0, 1}}}});
      f.add({{{y, z}, contradiction ? std::vector<Partition>{{0, 1}} : partitions(2)}});
      assert(solve_with_sat(f, guided) == !contradiction);
    }
  }
  Fixture f;
  const auto sort = f.symbols.int_sort();
  const auto x = f.variable("x", sort);
  const auto y = f.variable("y", sort);
  const auto z = f.variable("z", sort);
  f.add({{{x, y, z}, {{0, 0, 1}}}});
  auto& changing = f.add({{{x, y, z}, partitions(3)}});
  changing.properties.capabilities.model_mutation = true;
  changing.mutation = [&](const auto& request) {
    theory::TheoryMutationResult result;
    result.status = theory::TheoryMutationStatus::Mutated;
    result.model.token = request.source;
    ++result.model.token.revision;
    // Introduce y=z even though the reference requested x=y. A heuristic is
    // allowed to fail its preference, but all its side effects must be checked.
    result.model.partitions = {{sort, {{{x}}, {{y, z}}}}};
    return result;
  };
  smt::TheoryCombinationPlan plan(f.terms, f.components, f.atoms, true);
  plan.register_interfaces(f.components);
  smt::ComponentModelCoordinator coordinator;
  std::vector<theory::InterfacePair> decisions;
  auto result = coordinator.reconcile(plan, 1, f.components, {}, [&](const auto& pair) {
    decisions.push_back(pair);
    return sat::Lit{static_cast<int>(decisions.size() + 10)};
  });
  assert(result.status == smt::ModelBasedCombinationStatus::DecideEquality);
  assert(decisions.size() == 2);
  assert(std::find(decisions.begin(), decisions.end(), theory::InterfacePair{y, z, sort}) !=
         decisions.end());
  const std::vector arrangement{
      theory::InterfaceRelation{x, y, sort, sat::Lit{42}, theory::InterfaceRelationValue::Equal}};
  result = coordinator.reconcile(plan, 2, f.components, arrangement,
                                 [](const auto&) { return sat::Lit{42}; });
  assert(result.status == smt::ModelBasedCombinationStatus::Unknown);
  assert(coordinator.statistics().rejected_mutations == 1);
  changing.mutation = [](const auto& request) {
    theory::TheoryMutationResult cancelled;
    cancelled.status = theory::TheoryMutationStatus::Cancelled;
    cancelled.model.token = request.source;
    return cancelled;
  };
  assert(coordinator.reconcile(plan, 3, f.components, {}, [](const auto&) { return sat::Lit{42}; })
             .status == smt::ModelBasedCombinationStatus::Cancelled);
}

}  // namespace

int main() {
  admission_and_normalization();
  sort_nonparticipants_and_disconnected_groups();
  exhaustive_three_and_four_component_sat();
  malformed_models_are_not_accepted_or_redecided();
  incremental_routing_backtracking_and_explanation_lifetime();
  nonconvex_disjunction_and_mutation_side_effects();
}
