#include "smt/theory_combination_plan.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <set>
#include <utility>

#include "core/term.hpp"

namespace primo::smt {
namespace {

const theory::ComponentSharedSort* shared_sort(const theory::ComponentDescriptor& descriptor,
                                               core::SortId sort) {
  for (const auto& declared : descriptor.shared_sorts) {
    if (declared.sort == sort) {
      return &declared;
    }
  }
  return nullptr;
}

// Only plan construction uses this small component-index disjoint set. It is
// not a theory congruence closure or a substitute for SAT arrangement checks.
class SharingGroups {
 public:
  explicit SharingGroups(std::size_t size) : parents_(size) {
    std::iota(parents_.begin(), parents_.end(), std::size_t{0});
  }
  std::size_t root(std::size_t index) {
    while (parents_[index] != index) {
      parents_[index] = parents_[parents_[index]];
      index = parents_[index];
    }
    return index;
  }
  void join(std::size_t lhs, std::size_t rhs) {
    lhs = root(lhs);
    rhs = root(rhs);
    parents_[std::max(lhs, rhs)] = std::min(lhs, rhs);
  }

 private:
  std::vector<std::size_t> parents_;
};

}  // namespace

TheoryCombinationPlan::TheoryCombinationPlan(const core::TermManager& terms,
                                             std::span<theory::TheoryComponent* const> components,
                                             std::span<const theory::ComponentAtom> atoms,
                                             bool model_guided)
    : policy_(model_guided ? CombinationPolicy::ModelGuided
                           : CombinationPolicy::CompleteArrangement),
      component_scopes_(components.size()),
      component_atoms_(components.size()) {
  if (components.empty()) {
    throw UnsupportedCombination("combination requires at least one component");
  }
  std::set<std::string> identities;
  std::set<std::string> signatures;
  std::set<core::SymbolId> symbols;
  for (const auto* component : components) {
    if (component == nullptr) {
      throw UnsupportedCombination("null theory component");
    }
    const auto& descriptor = component->descriptor();
    if (descriptor.id.empty() || descriptor.fragment.empty() ||
        !identities.insert(descriptor.id).second) {
      throw UnsupportedCombination("missing or duplicate component identity/fragment");
    }
    for (const auto& family : descriptor.signature_families) {
      if (family.empty() || !signatures.insert(family).second) {
        throw UnsupportedCombination("overlapping interpreted signature: " + family);
      }
    }
    for (const auto symbol : descriptor.function_symbols) {
      if (symbol >= terms.symbols().symbols().size() ||
          terms.symbols().get_symbol(symbol).kind == core::SymbolKind::Constant ||
          !symbols.insert(symbol).second) {
        throw UnsupportedCombination("overlapping function symbol ownership");
      }
    }
    std::set<core::SortId> declared_sorts;
    for (const auto& declaration : descriptor.shared_sorts) {
      if (declaration.sort >= terms.symbols().sorts().size() ||
          !declared_sorts.insert(declaration.sort).second) {
        throw UnsupportedCombination("invalid or duplicate shared sort declaration");
      }
    }
    if (!descriptor.capabilities.model_projection) {
      policy_ = CombinationPolicy::CompleteArrangement;
    }
    descriptors_.push_back(descriptor);
  }

  // Deterministic sort/term occurrence index. Only genuinely shared variables
  // enter normalized scopes; private variables do not enlarge the old pair set.
  std::map<core::SortId, std::map<core::TermId, std::set<std::size_t>>> occurrences;
  for (const auto& atom : atoms) {
    if (atom.term >= terms.size() || atom.lit.value == 0 ||
        terms.get(atom.term).sort != terms.symbols().bool_sort()) {
      throw UnsupportedCombination("invalid registered theory atom");
    }
    std::optional<std::size_t> atom_owner;
    for (std::size_t index = 0; index < components.size(); ++index) {
      if (components[index]->owns_atom(atom.term)) {
        if (atom_owner.has_value()) {
          throw UnsupportedCombination("ambiguous theory atom ownership");
        }
        atom_owner = index;
      }
    }
    if (!atom_owner.has_value()) {
      throw UnsupportedCombination("unowned theory atom");
    }
    if (!owners_.emplace(atom.term, *atom_owner).second) {
      throw UnsupportedCombination("duplicate registered theory atom");
    }
    component_atoms_[*atom_owner].push_back(atom);
    for (const auto term : components[*atom_owner]->interface_variables(atom.term)) {
      if (term >= terms.size()) {
        throw UnsupportedCombination("invalid interface term ID");
      }
      const auto& variable = terms.get(term);
      if (variable.kind != core::TermKind::Constant) {
        throw UnsupportedCombination("interface term is not a purified free constant");
      }
      occurrences[variable.sort][term].insert(*atom_owner);
    }
  }

  for (const auto& [sort, by_term] : occurrences) {
    SharingGroups groups(components.size());
    for (const auto& [term, participants] : by_term) {
      static_cast<void>(term);
      for (const auto participant : participants) {
        groups.join(*participants.begin(), participant);
      }
    }
    std::map<std::size_t, CombinationScope> normalized;
    for (const auto& [term, participants] : by_term) {
      if (participants.size() < 2) {
        continue;
      }
      auto& scope = normalized[groups.root(*participants.begin())];
      scope.sort = sort;
      scope.terms.push_back(term);
      scope.participants.insert(scope.participants.end(), participants.begin(), participants.end());
    }
    for (auto& [root, scope] : normalized) {
      static_cast<void>(root);
      std::sort(scope.participants.begin(), scope.participants.end());
      scope.participants.erase(std::unique(scope.participants.begin(), scope.participants.end()),
                               scope.participants.end());
      std::size_t value_owners = 0;
      for (const auto participant : scope.participants) {
        const auto& descriptor = descriptors_[participant];
        const auto* declaration = shared_sort(descriptor, sort);
        if (declaration == nullptr ||
            declaration->stable_infiniteness != theory::StableInfiniteness::Yes ||
            sort == terms.symbols().bool_sort()) {
          throw UnsupportedCombination("shared sort lacks joint stable-infiniteness guarantee");
        }
        value_owners += declaration->authoritative_values ? 1U : 0U;
        for (const auto term : scope.terms) {
          if (!by_term.at(term).contains(participant) &&
              !descriptor.capabilities.interface_extension) {
            throw UnsupportedCombination("component cannot extend its shared interface");
          }
        }
        component_scopes_[participant].push_back({sort, scope.terms});
      }
      if (value_owners > 1) {
        throw UnsupportedCombination("multiple authoritative owners of a shared sort");
      }
      for (const auto term : scope.terms) {
        scope_by_term_.emplace(term, scopes_.size());
      }
      scopes_.push_back(std::move(scope));
    }
  }
}

bool TheoryCombinationPlan::convex_complete_propagation() const {
  return std::all_of(descriptors_.begin(), descriptors_.end(), [](const auto& descriptor) {
    return descriptor.convexity == theory::Convexity::Convex &&
           descriptor.capabilities.equality_propagation ==
               theory::EqualityPropagationStrength::Complete;
  });
}

std::size_t TheoryCombinationPlan::owner(core::TermId atom) const {
  const auto found = owners_.find(atom);
  if (found == owners_.end()) {
    throw UnsupportedCombination("theory atom has no registered owner");
  }
  return found->second;
}

std::optional<std::size_t> TheoryCombinationPlan::scope_for(
    const theory::InterfacePair& pair) const {
  const auto lhs = scope_by_term_.find(pair.lhs);
  const auto rhs = scope_by_term_.find(pair.rhs);
  if (pair.lhs == pair.rhs || lhs == scope_by_term_.end() || rhs == scope_by_term_.end() ||
      lhs->second != rhs->second || scopes_[lhs->second].sort != pair.sort) {
    return std::nullopt;
  }
  return lhs->second;
}

void TheoryCombinationPlan::register_interfaces(
    std::span<theory::TheoryComponent* const> components) const {
  if (components.size() != descriptors_.size()) {
    throw UnsupportedCombination("component collection changed after planning");
  }
  // Validate the entire collection before invoking any mutating registration.
  for (std::size_t index = 0; index < components.size(); ++index) {
    if (components[index] == nullptr || components[index]->descriptor() != descriptors_[index]) {
      throw UnsupportedCombination("component order changed after planning");
    }
  }
  for (std::size_t index = 0; index < components.size(); ++index) {
    components[index]->register_interface(component_scopes_[index]);
  }
}

}  // namespace primo::smt
