#include "smt/preprocessing.hpp"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"

namespace primo::smt {

namespace {

struct PairHash {
  std::size_t operator()(const std::pair<core::TermId, core::TermId>& pair) const {
    return (static_cast<std::size_t>(pair.first) << 32U) ^ pair.second;
  }
};

struct FiniteDomainChoice {
  std::vector<core::TermId> values;
};

struct SymmetryTerm {
  core::TermId term = core::kInvalidTermId;
};

struct SymmetryGroup {
  std::vector<core::TermId> values;
  std::vector<SymmetryTerm> terms;
};

struct BranchEqualityClosure {
  std::unordered_map<core::TermId, core::TermId> parent;
  std::unordered_set<core::TermId> nodes;

  core::TermId find(core::TermId term) {
    core::TermId root = term;
    while (true) {
      const auto parent_it = parent.find(root);
      if (parent_it == parent.end()) {
        break;
      }
      root = parent_it->second;
    }
    while (term != root) {
      const auto parent_it = parent.find(term);
      if (parent_it == parent.end()) {
        break;
      }
      const core::TermId next = parent_it->second;
      parent_it->second = root;
      term = next;
    }
    return root;
  }

  void unite(core::TermId lhs, core::TermId rhs) {
    nodes.insert(lhs);
    nodes.insert(rhs);
    lhs = find(lhs);
    rhs = find(rhs);
    if (lhs != rhs) {
      parent[lhs] = rhs;
    }
  }

  bool same(core::TermId lhs, core::TermId rhs) { return find(lhs) == find(rhs); }
};

constexpr std::size_t kMaxBridgeBranches = 64;
constexpr std::size_t kMaxBridgePairChecks = 200000;

std::pair<core::TermId, core::TermId> ordered_pair(core::TermId lhs, core::TermId rhs) {
  if (rhs < lhs) {
    std::swap(lhs, rhs);
  }
  return {lhs, rhs};
}

void collect_conjuncts(const core::TermManager& terms, core::TermId term,
                       std::vector<core::TermId>& conjuncts) {
  const core::Term& node = terms.get(term);
  if (node.kind == core::TermKind::And) {
    for (const core::TermId child : node.children) {
      collect_conjuncts(terms, child, conjuncts);
    }
    return;
  }
  conjuncts.push_back(term);
}

void collect_branch_equalities(const core::TermManager& terms, core::TermId branch,
                               BranchEqualityClosure& closure) {
  std::vector<core::TermId> work{branch};
  while (!work.empty()) {
    const core::TermId term = work.back();
    work.pop_back();
    const core::Term& node = terms.get(term);
    if (node.kind == core::TermKind::Equality) {
      closure.unite(node.children.at(0), node.children.at(1));
      continue;
    }
    if (node.kind == core::TermKind::And) {
      work.insert(work.end(), node.children.begin(), node.children.end());
    }
  }
}

void collect_or_branches(const core::TermManager& terms, core::TermId term,
                         std::vector<core::TermId>& branches) {
  std::vector<core::TermId> work{term};
  while (!work.empty()) {
    const core::TermId current = work.back();
    work.pop_back();
    const core::Term& node = terms.get(current);
    if (node.kind == core::TermKind::Or) {
      work.insert(work.end(), node.children.begin(), node.children.end());
      continue;
    }
    branches.push_back(current);
  }
}

void bridge_or_node(core::TermManager& terms, core::TermId term, EqualityBridgeResult& result) {
  std::vector<core::TermId> branches;
  collect_or_branches(terms, term, branches);
  if (branches.size() < 2) {
    return;
  }
  if (branches.size() > kMaxBridgeBranches) {
    ++result.skipped_or_nodes;
    return;
  }

  std::vector<BranchEqualityClosure> closures(branches.size());
  for (std::size_t i = 0; i < branches.size(); ++i) {
    collect_branch_equalities(terms, branches[i], closures[i]);
  }

  std::unordered_set<core::TermId> shared = closures.front().nodes;
  for (std::size_t i = 1; i < closures.size(); ++i) {
    std::unordered_set<core::TermId> next_shared;
    for (const core::TermId candidate : shared) {
      if (closures[i].nodes.contains(candidate)) {
        next_shared.insert(candidate);
      }
    }
    shared = std::move(next_shared);
    if (shared.empty()) {
      return;
    }
  }

  const std::vector<core::TermId> shared_nodes(shared.begin(), shared.end());
  const std::size_t pair_count = shared_nodes.size() * (shared_nodes.size() - 1U) / 2U;
  if (pair_count > kMaxBridgePairChecks / branches.size()) {
    ++result.skipped_or_nodes;
    return;
  }

  std::unordered_set<std::pair<core::TermId, core::TermId>, PairHash> emitted;
  for (std::size_t i = 0; i < shared_nodes.size(); ++i) {
    for (std::size_t j = i + 1; j < shared_nodes.size(); ++j) {
      const core::TermId lhs = shared_nodes[i];
      const core::TermId rhs = shared_nodes[j];
      bool common = true;
      for (BranchEqualityClosure& closure : closures) {
        if (!closure.same(lhs, rhs)) {
          common = false;
          break;
        }
      }
      if (common && emitted.insert(ordered_pair(lhs, rhs)).second) {
        result.equalities.push_back(terms.mk_eq(lhs, rhs));
      }
    }
  }
}

void collect_equality_bridges(core::TermManager& terms, core::TermId assertion,
                              EqualityBridgeResult& result) {
  std::vector<core::TermId> work{assertion};
  while (!work.empty()) {
    const core::TermId term = work.back();
    work.pop_back();
    const core::Term& node = terms.get(term);
    if (node.kind == core::TermKind::Or) {
      bridge_or_node(terms, term, result);
      continue;
    }
    if (node.kind == core::TermKind::And) {
      work.insert(work.end(), node.children.begin(), node.children.end());
    }
  }
}

void collect_unit_equalities(const core::TermManager& terms, core::TermId assertion,
                             BranchEqualityClosure& closure) {
  std::vector<core::TermId> work{assertion};
  while (!work.empty()) {
    const core::TermId term = work.back();
    work.pop_back();
    const core::Term& node = terms.get(term);
    if (node.kind == core::TermKind::Equality) {
      closure.unite(node.children.at(0), node.children.at(1));
      continue;
    }
    if (node.kind == core::TermKind::And) {
      work.insert(work.end(), node.children.begin(), node.children.end());
    }
  }
}

core::TermId simplify_with_equalities(core::TermManager& terms, core::TermId term,
                                      BranchEqualityClosure& closure) {
  const core::Term& node = terms.get(term);
  switch (node.kind) {
    case core::TermKind::BoolValue:
    case core::TermKind::Constant:
    case core::TermKind::Application:
      return term;
    case core::TermKind::Equality:
      return closure.same(node.children.at(0), node.children.at(1)) ? terms.mk_bool(true) : term;
    case core::TermKind::Not: {
      const core::TermId child = simplify_with_equalities(terms, node.children.at(0), closure);
      return terms.mk_not(child);
    }
    case core::TermKind::And: {
      std::vector<core::TermId> children;
      children.reserve(node.children.size());
      for (const core::TermId child : node.children) {
        children.push_back(simplify_with_equalities(terms, child, closure));
      }
      return terms.mk_and(std::move(children));
    }
    case core::TermKind::Or: {
      std::vector<core::TermId> children;
      children.reserve(node.children.size());
      for (const core::TermId child : node.children) {
        children.push_back(simplify_with_equalities(terms, child, closure));
      }
      return terms.mk_or(std::move(children));
    }
    case core::TermKind::Implies:
      return terms.mk_implies(simplify_with_equalities(terms, node.children.at(0), closure),
                              simplify_with_equalities(terms, node.children.at(1), closure));
  }
  return term;
}

std::unordered_set<std::pair<core::TermId, core::TermId>, PairHash> collect_disequalities(
    const core::TermManager& terms, const std::vector<core::TermId>& assertions) {
  std::vector<core::TermId> conjuncts;
  for (const core::TermId assertion : assertions) {
    collect_conjuncts(terms, assertion, conjuncts);
  }

  std::unordered_set<std::pair<core::TermId, core::TermId>, PairHash> disequalities;
  for (const core::TermId conjunct : conjuncts) {
    const core::Term& node = terms.get(conjunct);
    if (node.kind != core::TermKind::Not) {
      continue;
    }
    const core::Term& child = terms.get(node.children.at(0));
    if (child.kind != core::TermKind::Equality) {
      continue;
    }
    disequalities.insert(ordered_pair(child.children.at(0), child.children.at(1)));
  }
  return disequalities;
}

bool all_pairwise_disequal(
    const std::vector<core::TermId>& values,
    const std::unordered_set<std::pair<core::TermId, core::TermId>, PairHash>& disequalities) {
  for (std::size_t i = 0; i < values.size(); ++i) {
    for (std::size_t j = i + 1; j < values.size(); ++j) {
      if (!disequalities.contains(ordered_pair(values[i], values[j]))) {
        return false;
      }
    }
  }
  return true;
}

bool try_choice_term(
    const core::TermManager& terms, core::TermId or_term, core::TermId candidate,
    const std::unordered_set<std::pair<core::TermId, core::TermId>, PairHash>& disequalities,
    FiniteDomainChoice& choice) {
  const core::Term& or_node = terms.get(or_term);
  std::vector<core::TermId> values;
  values.reserve(or_node.children.size());

  for (const core::TermId child_id : or_node.children) {
    const core::Term& child = terms.get(child_id);
    if (child.kind != core::TermKind::Equality) {
      return false;
    }
    const core::TermId lhs = child.children.at(0);
    const core::TermId rhs = child.children.at(1);
    if (lhs == candidate && rhs != candidate) {
      values.push_back(rhs);
    } else if (rhs == candidate && lhs != candidate) {
      values.push_back(lhs);
    } else {
      return false;
    }
  }

  if (values.size() < 2) {
    return false;
  }
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
  if (values.size() < 2 || !all_pairwise_disequal(values, disequalities)) {
    return false;
  }

  choice.values = std::move(values);
  return true;
}

std::unordered_map<core::TermId, FiniteDomainChoice> collect_finite_domain_choices(
    const core::TermManager& terms, const std::vector<core::TermId>& assertions,
    const std::unordered_set<std::pair<core::TermId, core::TermId>, PairHash>& disequalities) {
  std::vector<core::TermId> work = assertions;
  std::unordered_map<core::TermId, FiniteDomainChoice> choices;

  while (!work.empty()) {
    const core::TermId term_id = work.back();
    work.pop_back();
    const core::Term& term = terms.get(term_id);

    if (term.kind == core::TermKind::And) {
      work.insert(work.end(), term.children.begin(), term.children.end());
      continue;
    }
    if (term.kind != core::TermKind::Or || term.children.size() < 2) {
      continue;
    }

    const core::Term& first = terms.get(term.children.front());
    if (first.kind != core::TermKind::Equality) {
      continue;
    }

    FiniteDomainChoice choice;
    const core::TermId lhs = first.children.at(0);
    const core::TermId rhs = first.children.at(1);
    core::TermId candidate = lhs;
    if (!try_choice_term(terms, term_id, candidate, disequalities, choice)) {
      candidate = rhs;
      if (!try_choice_term(terms, term_id, candidate, disequalities, choice)) {
        continue;
      }
    }

    const auto existing = choices.find(candidate);
    if (existing == choices.end() || existing->second.values.size() < choice.values.size()) {
      choices[candidate] = std::move(choice);
    }
  }

  return choices;
}

bool all_values_are_constants(const core::TermManager& terms,
                              const std::vector<core::TermId>& values) {
  for (const core::TermId value : values) {
    const core::Term& term = terms.get(value);
    if (term.kind != core::TermKind::Constant || term.sort == terms.symbols().bool_sort()) {
      return false;
    }
  }
  return true;
}

bool contains_any_term(const core::TermManager& terms, core::TermId root,
                       const std::unordered_set<core::TermId>& needles) {
  std::vector<core::TermId> work{root};
  while (!work.empty()) {
    const core::TermId term = work.back();
    work.pop_back();
    if (needles.contains(term)) {
      return true;
    }
    const std::vector<core::TermId>& children = terms.get(term).children;
    work.insert(work.end(), children.begin(), children.end());
  }
  return false;
}

void remove_terms_containing_values(const core::TermManager& terms, SymmetryGroup& group) {
  std::unordered_set<core::TermId> value_set(group.values.begin(), group.values.end());
  group.terms.erase(std::remove_if(group.terms.begin(), group.terms.end(),
                                   [&](const SymmetryTerm& term) {
                                     return contains_any_term(terms, term.term, value_set);
                                   }),
                    group.terms.end());
}

std::string canonical_key(const core::TermManager& terms, core::TermId root,
                          const std::unordered_map<core::TermId, core::TermId>& permutation,
                          std::unordered_map<core::TermId, std::string>& memo) {
  if (const auto found = memo.find(root); found != memo.end()) {
    return found->second;
  }

  const core::Term& term = terms.get(root);
  if (term.kind == core::TermKind::Constant) {
    if (const auto mapped = permutation.find(root); mapped != permutation.end()) {
      return memo[root] = "const-term:" + std::to_string(mapped->second);
    }
  }

  std::vector<std::string> children;
  children.reserve(term.children.size());
  for (const core::TermId child : term.children) {
    children.push_back(canonical_key(terms, child, permutation, memo));
  }
  if (term.kind == core::TermKind::Equality || term.kind == core::TermKind::And ||
      term.kind == core::TermKind::Or) {
    std::sort(children.begin(), children.end());
  }

  std::ostringstream key;
  key << static_cast<int>(term.kind) << ':' << term.sort << ':' << term.symbol << ':'
      << (term.bool_value ? 1 : 0) << '(';
  for (const std::string& child : children) {
    key << child << ',';
  }
  key << ')';
  return memo[root] = key.str();
}

std::vector<std::string> canonical_formula_multiset(
    const core::TermManager& terms, const std::vector<core::TermId>& assertions,
    const std::unordered_map<core::TermId, core::TermId>& permutation) {
  std::vector<std::string> keys;
  keys.reserve(assertions.size());
  std::unordered_map<core::TermId, std::string> memo;
  for (const core::TermId assertion : assertions) {
    keys.push_back(canonical_key(terms, assertion, permutation, memo));
  }
  std::sort(keys.begin(), keys.end());
  return keys;
}

std::unordered_map<core::TermId, core::TermId> identity_permutation(
    const std::vector<core::TermId>& values) {
  std::unordered_map<core::TermId, core::TermId> permutation;
  permutation.reserve(values.size());
  for (const core::TermId value : values) {
    permutation[value] = value;
  }
  return permutation;
}

std::unordered_map<core::TermId, core::TermId> cycle_permutation(
    const std::vector<core::TermId>& values) {
  std::unordered_map<core::TermId, core::TermId> permutation;
  permutation.reserve(values.size());
  for (std::size_t i = 0; i < values.size(); ++i) {
    permutation[values[i]] = values[(i + values.size() - 1) % values.size()];
  }
  return permutation;
}

std::unordered_map<core::TermId, core::TermId> swap_first_two_permutation(
    const std::vector<core::TermId>& values) {
  std::unordered_map<core::TermId, core::TermId> permutation = identity_permutation(values);
  if (values.size() >= 2) {
    permutation[values[0]] = values[1];
    permutation[values[1]] = values[0];
  }
  return permutation;
}

bool formulas_are_invariant_under_value_permutations(const core::TermManager& terms,
                                                     const std::vector<core::TermId>& assertions,
                                                     const std::vector<core::TermId>& values) {
  // Déharbe, Fontaine, Merz, and Woltzenlogel Paleo's SMT symmetry-breaking
  // theorem applies when the formula is invariant under permutations of the
  // candidate constants. We use the cycle plus first-swap generators as the
  // cheap syntactic check, matching the llm2smt QF_UF pass.
  const std::vector<std::string> base =
      canonical_formula_multiset(terms, assertions, identity_permutation(values));
  return base == canonical_formula_multiset(terms, assertions, cycle_permutation(values)) &&
         base == canonical_formula_multiset(terms, assertions, swap_first_two_permutation(values));
}

std::vector<SymmetryGroup> build_groups(
    const core::TermManager& terms,
    const std::unordered_map<core::TermId, FiniteDomainChoice>& choices) {
  std::vector<SymmetryGroup> groups;
  for (const auto& [term, choice] : choices) {
    if (choice.values.size() < 2 || !all_values_are_constants(terms, choice.values)) {
      continue;
    }
    auto group = std::find_if(groups.begin(), groups.end(), [&](const SymmetryGroup& candidate) {
      return candidate.values == choice.values;
    });
    if (group == groups.end()) {
      SymmetryGroup new_group;
      new_group.values = choice.values;
      groups.push_back(std::move(new_group));
      group = groups.end() - 1;
    }
    group->terms.push_back(SymmetryTerm{term});
  }
  return groups;
}

core::TermId first_use_requires_previous_value(core::TermManager& terms, core::TermId term,
                                               const std::vector<SymmetryTerm>& previous_terms,
                                               core::TermId value, core::TermId previous_value) {
  std::vector<core::TermId> previous_value_seen;
  previous_value_seen.reserve(previous_terms.size());
  for (const SymmetryTerm& previous_term : previous_terms) {
    previous_value_seen.push_back(terms.mk_eq(previous_term.term, previous_value));
  }
  return terms.mk_implies(terms.mk_eq(term, value), terms.mk_or(std::move(previous_value_seen)));
}

void add_value_precedence_constraints(core::TermManager& terms, const SymmetryGroup& group,
                                      UfSymmetryBreakingResult& result) {
  // Reger and Suda describe finite-model value ordering as an efficiency device;
  // here it is guarded by the SMT invariance check above rather than by a full
  // finite-model SAT reduction.
  for (std::size_t i = 0; i < group.terms.size(); ++i) {
    const core::TermId term = group.terms[i].term;
    for (std::size_t j = i + 1; j < group.values.size(); ++j) {
      result.constraints.push_back(terms.mk_not(terms.mk_eq(term, group.values[j])));
    }

    const std::size_t max_j = std::min(i, group.values.size() - 1);
    std::vector<SymmetryTerm> previous_terms;
    previous_terms.reserve(i);
    for (std::size_t previous_index = 0; previous_index < i; ++previous_index) {
      previous_terms.push_back(group.terms[previous_index]);
    }
    for (std::size_t j = 1; j <= max_j; ++j) {
      result.constraints.push_back(first_use_requires_previous_value(
          terms, term, previous_terms, group.values[j], group.values[j - 1]));
    }
  }
}

}  // namespace

EqualityBridge::EqualityBridge(core::TermManager& terms) : terms_(terms) {}

EqualityBridgeResult EqualityBridge::bridge(const std::vector<core::TermId>& assertions) {
  EqualityBridgeResult result;
  for (const core::TermId assertion : assertions) {
    collect_equality_bridges(terms_, assertion, result);
  }
  return result;
}

std::vector<core::TermId> propagate_unit_equalities(core::TermManager& terms,
                                                    const std::vector<core::TermId>& assertions,
                                                    std::size_t* consequences_added) {
  // This is a deliberately small preprocessing closure, not a theory-solver
  // shortcut. Unit equalities already asserted at top level, including facts
  // derived by the equality bridge, are used to simplify equality atoms in the
  // whole formula before Boolean abstraction.
  BranchEqualityClosure closure;
  for (const core::TermId assertion : assertions) {
    collect_unit_equalities(terms, assertion, closure);
  }

  std::vector<core::TermId> simplified;
  simplified.reserve(assertions.size());
  std::unordered_set<core::TermId> emitted;
  std::size_t added = 0;
  for (const core::TermId assertion : assertions) {
    if (emitted.insert(assertion).second) {
      simplified.push_back(assertion);
    }
    const core::TermId consequence = simplify_with_equalities(terms, assertion, closure);
    const core::Term& consequence_node = terms.get(consequence);
    const bool true_consequence =
        consequence_node.kind == core::TermKind::BoolValue && consequence_node.bool_value;
    if (consequence != assertion && !true_consequence && emitted.insert(consequence).second) {
      simplified.push_back(consequence);
      ++added;
    }
  }
  if (consequences_added != nullptr) {
    *consequences_added = added;
  }
  return simplified;
}

UfSymmetryBreaker::UfSymmetryBreaker(core::TermManager& terms) : terms_(terms) {}

UfSymmetryBreakingResult UfSymmetryBreaker::break_symmetries(
    const std::vector<core::TermId>& assertions) {
  UfSymmetryBreakingResult result;
  const auto disequalities = collect_disequalities(terms_, assertions);
  const auto choices = collect_finite_domain_choices(terms_, assertions, disequalities);
  std::vector<SymmetryGroup> groups = build_groups(terms_, choices);

  for (SymmetryGroup& group : groups) {
    remove_terms_containing_values(terms_, group);
    if (group.terms.empty()) {
      ++result.rejected_noninvariant;
      continue;
    }
    if (!formulas_are_invariant_under_value_permutations(terms_, assertions, group.values)) {
      ++result.rejected_noninvariant;
      continue;
    }

    std::sort(group.terms.begin(), group.terms.end(),
              [](const SymmetryTerm& lhs, const SymmetryTerm& rhs) { return lhs.term < rhs.term; });
    ++result.accepted_sets;
    result.values += group.values.size();
    result.terms += group.terms.size();
    add_value_precedence_constraints(terms_, group, result);
  }

  return result;
}

}  // namespace primo::smt
