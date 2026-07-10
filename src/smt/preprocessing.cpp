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
#include "theory/equality/congruence_closure.hpp"

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

struct FiniteDomainGroup {
  std::vector<core::TermId> values;
  std::vector<core::TermId> terms;
};

struct AppValueKey {
  core::SymbolId symbol = core::kInvalidSymbolId;
  std::vector<core::TermId> args;

  bool operator==(const AppValueKey& other) const {
    return symbol == other.symbol && args == other.args;
  }
};

struct AppValueKeyHash {
  std::size_t operator()(const AppValueKey& key) const {
    std::size_t seed = static_cast<std::size_t>(key.symbol);
    for (const core::TermId arg : key.args) {
      seed ^= static_cast<std::size_t>(arg) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    }
    return seed;
  }
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
constexpr std::size_t kMaxCongruenceDefinitionClauses = 50000;
constexpr std::size_t kMaxFiniteDomainEvaluationAssignments = 250000;

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

void collect_applications(const core::TermManager& terms, core::TermId root,
                          std::unordered_set<core::TermId>& seen,
                          std::unordered_map<core::SymbolId, std::vector<core::TermId>>& groups) {
  if (!seen.insert(root).second) {
    return;
  }
  const core::Term& term = terms.get(root);
  if (term.kind == core::TermKind::Application) {
    groups[term.symbol].push_back(root);
  }
  for (const core::TermId child : term.children) {
    collect_applications(terms, child, seen, groups);
  }
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

void collect_or_literals(const core::TermManager& terms, core::TermId term,
                         std::vector<core::TermId>& literals) {
  std::vector<core::TermId> work{term};
  while (!work.empty()) {
    const core::TermId current = work.back();
    work.pop_back();
    const core::Term& node = terms.get(current);
    if (node.kind == core::TermKind::Or) {
      work.insert(work.end(), node.children.begin(), node.children.end());
      continue;
    }
    literals.push_back(current);
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
                             theory::equality::CongruenceClosure& closure) {
  std::vector<core::TermId> work{assertion};
  while (!work.empty()) {
    const core::TermId term = work.back();
    work.pop_back();
    const core::Term& node = terms.get(term);
    if (node.kind == core::TermKind::Equality) {
      closure.assert_equal(node.children.at(0), node.children.at(1));
      continue;
    }
    if (node.kind == core::TermKind::And) {
      work.insert(work.end(), node.children.begin(), node.children.end());
    }
  }
}

core::TermId simplify_with_congruence(core::TermManager& terms, core::TermId term,
                                      theory::equality::CongruenceClosure& closure) {
  const core::Term& node = terms.get(term);
  switch (node.kind) {
    case core::TermKind::BoolValue:
    case core::TermKind::Constant:
    case core::TermKind::Application:
      return term;
    case core::TermKind::Equality:
      return closure.are_equal(node.children.at(0), node.children.at(1)) ? terms.mk_bool(true)
                                                                         : term;
    case core::TermKind::Not: {
      const core::TermId child = simplify_with_congruence(terms, node.children.at(0), closure);
      return terms.mk_not(child);
    }
    case core::TermKind::And: {
      std::vector<core::TermId> children;
      children.reserve(node.children.size());
      for (const core::TermId child : node.children) {
        children.push_back(simplify_with_congruence(terms, child, closure));
      }
      return terms.mk_and(std::move(children));
    }
    case core::TermKind::Or: {
      std::vector<core::TermId> children;
      children.reserve(node.children.size());
      for (const core::TermId child : node.children) {
        children.push_back(simplify_with_congruence(terms, child, closure));
      }
      return terms.mk_or(std::move(children));
    }
    case core::TermKind::Implies:
      return terms.mk_implies(simplify_with_congruence(terms, node.children.at(0), closure),
                              simplify_with_congruence(terms, node.children.at(1), closure));
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
  std::vector<core::TermId> literals;
  collect_or_literals(terms, or_term, literals);
  std::vector<core::TermId> values;
  values.reserve(literals.size());

  for (const core::TermId child_id : literals) {
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

bool all_values_are_constants(const core::TermManager& terms,
                              const std::vector<core::TermId>& values);

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

std::vector<FiniteDomainGroup> finite_domain_groups(
    const core::TermManager& terms,
    const std::unordered_map<core::TermId, FiniteDomainChoice>& choices,
    bool require_constant_values) {
  std::vector<FiniteDomainGroup> groups;
  for (const auto& [term, choice] : choices) {
    if (choice.values.size() < 2) {
      continue;
    }
    if (require_constant_values && !all_values_are_constants(terms, choice.values)) {
      continue;
    }
    auto group = std::find_if(groups.begin(), groups.end(), [&](const FiniteDomainGroup& item) {
      return item.values == choice.values;
    });
    if (group == groups.end()) {
      FiniteDomainGroup new_group;
      new_group.values = choice.values;
      groups.push_back(std::move(new_group));
      group = groups.end() - 1;
    }
    group->terms.push_back(term);
  }
  return groups;
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

std::vector<core::TermId> finite_domain_at_most_one_clauses(
    core::TermManager& terms, core::TermId term, const std::vector<core::TermId>& values) {
  std::vector<core::TermId> clauses;
  const std::size_t pair_count = values.size() * (values.size() - 1U) / 2U;
  clauses.reserve(pair_count);
  for (std::size_t i = 0; i < values.size(); ++i) {
    for (std::size_t j = i + 1; j < values.size(); ++j) {
      clauses.push_back(terms.mk_or({terms.mk_not(terms.mk_eq(term, values[i])),
                                     terms.mk_not(terms.mk_eq(term, values[j]))}));
    }
  }
  return clauses;
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
  for (const FiniteDomainGroup& finite_group : finite_domain_groups(terms, choices, true)) {
    auto group = std::find_if(groups.begin(), groups.end(), [&](const SymmetryGroup& candidate) {
      return candidate.values == finite_group.values;
    });
    if (group == groups.end()) {
      SymmetryGroup new_group;
      new_group.values = finite_group.values;
      groups.push_back(std::move(new_group));
      group = groups.end() - 1;
    }
    for (const core::TermId term : finite_group.terms) {
      group->terms.push_back(SymmetryTerm{term});
    }
  }
  return groups;
}

std::vector<core::TermId> finite_domain_equality_definition_clauses(
    core::TermManager& terms, core::TermId lhs, core::TermId rhs,
    const std::vector<core::TermId>& values) {
  std::vector<core::TermId> clauses;
  clauses.reserve(values.size() * 3);
  const core::TermId lhs_eq_rhs = terms.mk_eq(lhs, rhs);
  for (const core::TermId value : values) {
    const core::TermId lhs_eq_value = terms.mk_eq(lhs, value);
    const core::TermId rhs_eq_value = terms.mk_eq(rhs, value);
    clauses.push_back(
        terms.mk_or({terms.mk_not(lhs_eq_value), terms.mk_not(rhs_eq_value), lhs_eq_rhs}));
    clauses.push_back(
        terms.mk_or({terms.mk_not(lhs_eq_rhs), terms.mk_not(lhs_eq_value), rhs_eq_value}));
    clauses.push_back(
        terms.mk_or({terms.mk_not(lhs_eq_rhs), terms.mk_not(rhs_eq_value), lhs_eq_value}));
  }
  return clauses;
}

void collect_unit_equalities_list(const core::TermManager& terms, core::TermId assertion,
                                  std::vector<std::pair<core::TermId, core::TermId>>& equalities) {
  std::vector<core::TermId> work{assertion};
  while (!work.empty()) {
    const core::TermId term = work.back();
    work.pop_back();
    const core::Term& node = terms.get(term);
    if (node.kind == core::TermKind::Equality) {
      equalities.emplace_back(node.children.at(0), node.children.at(1));
      continue;
    }
    if (node.kind == core::TermKind::And) {
      work.insert(work.end(), node.children.begin(), node.children.end());
    }
  }
}

bool is_domain_value(core::TermId term, const std::unordered_set<core::TermId>& values) {
  return values.contains(term);
}

class FiniteDomainEvaluationState {
 public:
  FiniteDomainEvaluationState(const core::TermManager& terms,
                              std::unordered_map<core::TermId, FiniteDomainChoice> choices,
                              std::vector<std::pair<core::TermId, core::TermId>> unit_equalities)
      : terms_(terms), choices_(std::move(choices)), unit_equalities_(std::move(unit_equalities)) {
    for (const auto& [term, choice] : choices_) {
      (void)term;
      domain_values_.insert(choice.values.begin(), choice.values.end());
    }
    build_fixed_values_and_tables();
  }

  bool always_equal(core::TermId lhs, core::TermId rhs, bool* cutoff) const {
    std::vector<core::TermId> variables;
    collect_variables(lhs, variables);
    collect_variables(rhs, variables);
    std::sort(variables.begin(), variables.end());
    variables.erase(std::unique(variables.begin(), variables.end()), variables.end());

    std::size_t combinations = 1;
    for (const core::TermId variable : variables) {
      const auto found = choices_.find(variable);
      if (found == choices_.end()) {
        return false;
      }
      if (found->second.values.empty() ||
          combinations > kMaxFiniteDomainEvaluationAssignments / found->second.values.size()) {
        *cutoff = true;
        return false;
      }
      combinations *= found->second.values.size();
    }

    std::unordered_map<core::TermId, core::TermId> assignment;
    return enumerate_equal(lhs, rhs, variables, 0, assignment);
  }

 private:
  std::optional<core::TermId> fixed_value(core::TermId term) const {
    if (is_domain_value(term, domain_values_)) {
      return term;
    }
    const auto found = fixed_values_.find(term);
    if (found != fixed_values_.end()) {
      return found->second;
    }
    return std::nullopt;
  }

  std::optional<core::TermId> eval_with_fixed_values(core::TermId term) const {
    if (const std::optional<core::TermId> fixed = fixed_value(term); fixed.has_value()) {
      return fixed;
    }
    const core::Term& node = terms_.get(term);
    if (node.kind != core::TermKind::Application) {
      return std::nullopt;
    }
    std::vector<core::TermId> args;
    args.reserve(node.children.size());
    for (const core::TermId child : node.children) {
      std::optional<core::TermId> value = eval_with_fixed_values(child);
      if (!value.has_value()) {
        return std::nullopt;
      }
      args.push_back(value.value());
    }
    const auto found = tables_.find(AppValueKey{node.symbol, std::move(args)});
    if (found == tables_.end()) {
      return std::nullopt;
    }
    return found->second;
  }

  void build_fixed_values_and_tables() {
    bool changed = true;
    while (changed) {
      changed = false;
      for (const auto& [lhs, rhs] : unit_equalities_) {
        changed = record_fixed_value(lhs, rhs) || changed;
        changed = record_fixed_value(rhs, lhs) || changed;
        changed = record_table_entry(lhs, rhs) || changed;
        changed = record_table_entry(rhs, lhs) || changed;
      }
    }
  }

  bool record_fixed_value(core::TermId term, core::TermId value_term) {
    const std::optional<core::TermId> value = eval_with_fixed_values(value_term);
    if (!value.has_value() || !is_domain_value(value.value(), domain_values_)) {
      return false;
    }
    const auto [_, inserted] = fixed_values_.emplace(term, value.value());
    return inserted;
  }

  bool record_table_entry(core::TermId app_term, core::TermId value_term) {
    const core::Term& app = terms_.get(app_term);
    if (app.kind != core::TermKind::Application) {
      return false;
    }
    const std::optional<core::TermId> value = eval_with_fixed_values(value_term);
    if (!value.has_value() || !is_domain_value(value.value(), domain_values_)) {
      return false;
    }
    std::vector<core::TermId> args;
    args.reserve(app.children.size());
    for (const core::TermId child : app.children) {
      std::optional<core::TermId> arg_value = eval_with_fixed_values(child);
      if (!arg_value.has_value()) {
        return false;
      }
      args.push_back(arg_value.value());
    }
    const auto [_, inserted] =
        tables_.emplace(AppValueKey{app.symbol, std::move(args)}, value.value());
    return inserted;
  }

  void collect_variables(core::TermId term, std::vector<core::TermId>& variables) const {
    if (eval_with_fixed_values(term).has_value()) {
      return;
    }
    if (choices_.contains(term)) {
      variables.push_back(term);
      return;
    }
    for (const core::TermId child : terms_.get(term).children) {
      collect_variables(child, variables);
    }
  }

  std::optional<core::TermId> eval(
      core::TermId term, const std::unordered_map<core::TermId, core::TermId>& assignment) const {
    if (const std::optional<core::TermId> fixed = fixed_value(term); fixed.has_value()) {
      return fixed;
    }
    const auto assigned = assignment.find(term);
    if (assigned != assignment.end()) {
      return assigned->second;
    }
    const core::Term& node = terms_.get(term);
    if (node.kind != core::TermKind::Application) {
      return std::nullopt;
    }
    std::vector<core::TermId> args;
    args.reserve(node.children.size());
    for (const core::TermId child : node.children) {
      std::optional<core::TermId> value = eval(child, assignment);
      if (!value.has_value()) {
        return std::nullopt;
      }
      args.push_back(value.value());
    }
    const auto found = tables_.find(AppValueKey{node.symbol, std::move(args)});
    if (found == tables_.end()) {
      return std::nullopt;
    }
    return found->second;
  }

  bool enumerate_equal(core::TermId lhs, core::TermId rhs, const std::vector<core::TermId>& vars,
                       std::size_t index,
                       std::unordered_map<core::TermId, core::TermId>& assignment) const {
    if (index == vars.size()) {
      const std::optional<core::TermId> lhs_value = eval(lhs, assignment);
      const std::optional<core::TermId> rhs_value = eval(rhs, assignment);
      return lhs_value.has_value() && rhs_value.has_value() &&
             lhs_value.value() == rhs_value.value();
    }
    const core::TermId variable = vars[index];
    const auto found = choices_.find(variable);
    if (found == choices_.end()) {
      return false;
    }
    for (const core::TermId value : found->second.values) {
      assignment[variable] = value;
      if (!enumerate_equal(lhs, rhs, vars, index + 1, assignment)) {
        assignment.erase(variable);
        return false;
      }
    }
    assignment.erase(variable);
    return true;
  }

  const core::TermManager& terms_;
  std::unordered_map<core::TermId, FiniteDomainChoice> choices_;
  std::vector<std::pair<core::TermId, core::TermId>> unit_equalities_;
  std::unordered_set<core::TermId> domain_values_;
  std::unordered_map<core::TermId, core::TermId> fixed_values_;
  std::unordered_map<AppValueKey, core::TermId, AppValueKeyHash> tables_;
};

core::TermId simplify_with_finite_domain_evaluation(core::TermManager& terms, core::TermId term,
                                                    const FiniteDomainEvaluationState& state,
                                                    FiniteDomainEvaluationResult& result) {
  const core::Term& node = terms.get(term);
  switch (node.kind) {
    case core::TermKind::BoolValue:
    case core::TermKind::Constant:
    case core::TermKind::Application:
      return term;
    case core::TermKind::Equality: {
      bool cutoff = false;
      if (state.always_equal(node.children.at(0), node.children.at(1), &cutoff)) {
        ++result.equalities_simplified;
        return terms.mk_bool(true);
      }
      if (cutoff) {
        ++result.enumeration_cutoffs;
      }
      return term;
    }
    case core::TermKind::Not:
      return terms.mk_not(
          simplify_with_finite_domain_evaluation(terms, node.children.at(0), state, result));
    case core::TermKind::And: {
      std::vector<core::TermId> children;
      children.reserve(node.children.size());
      for (const core::TermId child : node.children) {
        children.push_back(simplify_with_finite_domain_evaluation(terms, child, state, result));
      }
      return terms.mk_and(std::move(children));
    }
    case core::TermKind::Or: {
      std::vector<core::TermId> children;
      children.reserve(node.children.size());
      for (const core::TermId child : node.children) {
        children.push_back(simplify_with_finite_domain_evaluation(terms, child, state, result));
      }
      return terms.mk_or(std::move(children));
    }
    case core::TermKind::Implies:
      return terms.mk_implies(
          simplify_with_finite_domain_evaluation(terms, node.children.at(0), state, result),
          simplify_with_finite_domain_evaluation(terms, node.children.at(1), state, result));
  }
  return term;
}

core::TermId congruence_definition_clause(core::TermManager& terms, core::TermId lhs,
                                          core::TermId rhs) {
  const core::Term& lhs_term = terms.get(lhs);
  const core::Term& rhs_term = terms.get(rhs);
  std::vector<core::TermId> literals;
  literals.reserve(lhs_term.children.size() + 1);
  for (std::size_t i = 0; i < lhs_term.children.size(); ++i) {
    literals.push_back(terms.mk_not(terms.mk_eq(lhs_term.children[i], rhs_term.children[i])));
  }
  literals.push_back(terms.mk_eq(lhs, rhs));
  return terms.mk_or(std::move(literals));
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
  // This is a deliberately small preprocessing closure, not a SAT/theory
  // shortcut. Unit equalities already asserted at top level, including facts
  // derived by the equality bridge, are fed to the same ground congruence
  // relation used by the EUF solver, then equality atoms decided by that unit
  // relation are simplified before Boolean abstraction.
  theory::equality::CongruenceClosure closure(terms);
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
    const core::TermId consequence = simplify_with_congruence(terms, assertion, closure);
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

FiniteDomainAtMostOne::FiniteDomainAtMostOne(core::TermManager& terms) : terms_(terms) {}

FiniteDomainAtMostOneResult FiniteDomainAtMostOne::generate(
    const std::vector<core::TermId>& assertions) {
  // Bradley and Manna's standard EUF congruence setting justifies these as
  // theory lemmas: if asserted finite-domain values are pairwise disequal, one
  // term cannot equal two different values. The pass only emits SAT-visible
  // AMO clauses for domains whose value disequalities are explicit top-level
  // facts in the current assertion vector.
  FiniteDomainAtMostOneResult result;
  const auto disequalities = collect_disequalities(terms_, assertions);
  const auto choices = collect_finite_domain_choices(terms_, assertions, disequalities);
  std::unordered_set<core::TermId> emitted(assertions.begin(), assertions.end());
  for (const auto& [term, choice] : choices) {
    const std::vector<core::TermId> clauses =
        finite_domain_at_most_one_clauses(terms_, term, choice.values);
    std::size_t added_for_domain = 0;
    for (const core::TermId clause : clauses) {
      if (emitted.insert(clause).second) {
        result.clauses.push_back(clause);
        ++added_for_domain;
      }
    }
    if (added_for_domain > 0) {
      ++result.domains;
    }
  }
  return result;
}

FiniteDomainEqualityDefinitions::FiniteDomainEqualityDefinitions(core::TermManager& terms)
    : terms_(terms) {}

FiniteDomainEqualityDefinitionsResult FiniteDomainEqualityDefinitions::generate(
    const std::vector<core::TermId>& assertions) {
  // Bradley and Manna's ground EUF setting justifies these finite-domain
  // definition clauses when every involved term has an explicit, pairwise
  // disequal choice domain. They expose (= s t) through the SAT-visible choices
  // (= s c_i) and (= t c_i), without adding fresh propositional variables.
  FiniteDomainEqualityDefinitionsResult result;
  const auto disequalities = collect_disequalities(terms_, assertions);
  const auto choices = collect_finite_domain_choices(terms_, assertions, disequalities);
  const std::vector<FiniteDomainGroup> groups = finite_domain_groups(terms_, choices, false);
  std::unordered_set<core::TermId> emitted(assertions.begin(), assertions.end());
  for (FiniteDomainGroup group : groups) {
    std::sort(group.terms.begin(), group.terms.end());
    group.terms.erase(std::unique(group.terms.begin(), group.terms.end()), group.terms.end());
    for (std::size_t i = 0; i < group.terms.size(); ++i) {
      for (std::size_t j = i + 1; j < group.terms.size(); ++j) {
        const std::vector<core::TermId> clauses = finite_domain_equality_definition_clauses(
            terms_, group.terms[i], group.terms[j], group.values);
        std::size_t added_for_pair = 0;
        for (const core::TermId clause : clauses) {
          if (emitted.insert(clause).second) {
            result.clauses.push_back(clause);
            ++added_for_pair;
          }
        }
        if (added_for_pair > 0) {
          ++result.term_pairs;
        }
      }
    }
  }
  return result;
}

FiniteDomainEvaluator::FiniteDomainEvaluator(core::TermManager& terms) : terms_(terms) {}

FiniteDomainEvaluationResult FiniteDomainEvaluator::simplify(
    const std::vector<core::TermId>& assertions) {
  FiniteDomainEvaluationResult result;
  result.assertions.reserve(assertions.size());
  const auto disequalities = collect_disequalities(terms_, assertions);
  const auto choices = collect_finite_domain_choices(terms_, assertions, disequalities);
  if (choices.empty()) {
    result.assertions = assertions;
    return result;
  }

  std::vector<std::pair<core::TermId, core::TermId>> unit_equalities;
  for (const core::TermId assertion : assertions) {
    collect_unit_equalities_list(terms_, assertion, unit_equalities);
  }
  FiniteDomainEvaluationState state(terms_, choices, std::move(unit_equalities));
  for (const core::TermId assertion : assertions) {
    result.assertions.push_back(
        simplify_with_finite_domain_evaluation(terms_, assertion, state, result));
  }
  return result;
}

CongruenceDefinitionGenerator::CongruenceDefinitionGenerator(core::TermManager& terms)
    : terms_(terms) {}

CongruenceDefinitionResult CongruenceDefinitionGenerator::generate(
    const std::vector<core::TermId>& assertions) {
  // Bradley and Manna's ground EUF calculus includes the congruence axiom:
  // equal arguments to the same uninterpreted function have equal results. The
  // lazy EUF solver can discover this, but PEQ/NEQ table benchmarks benefit
  // from a bounded SAT-visible subset over applications already present in the
  // formula. No fresh terms are invented.
  CongruenceDefinitionResult result;
  std::unordered_set<core::TermId> seen;
  std::unordered_map<core::SymbolId, std::vector<core::TermId>> groups;
  for (const core::TermId assertion : assertions) {
    collect_applications(terms_, assertion, seen, groups);
  }

  std::unordered_set<core::TermId> emitted(assertions.begin(), assertions.end());
  for (auto& [symbol, applications] : groups) {
    (void)symbol;
    std::sort(applications.begin(), applications.end());
    applications.erase(std::unique(applications.begin(), applications.end()), applications.end());
    if (applications.size() < 2) {
      continue;
    }
    ++result.groups;
    bool skipped = false;
    for (std::size_t i = 0; i < applications.size() && !skipped; ++i) {
      for (std::size_t j = i + 1; j < applications.size(); ++j) {
        if (result.clauses.size() >= kMaxCongruenceDefinitionClauses) {
          skipped = true;
          break;
        }
        const core::TermId clause =
            congruence_definition_clause(terms_, applications[i], applications[j]);
        if (emitted.insert(clause).second) {
          result.clauses.push_back(clause);
        }
      }
    }
    if (skipped) {
      ++result.skipped_groups;
      break;
    }
  }
  return result;
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
