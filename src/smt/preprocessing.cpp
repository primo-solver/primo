#include "smt/preprocessing.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/symbol_table.hpp"
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
  std::vector<core::TermId> used_values;
  std::size_t occurrences = 0;
};

struct SymmetryGroup {
  std::vector<core::TermId> values;
  std::vector<SymmetryTerm> terms;
};

struct FiniteDomainGroup {
  std::vector<core::TermId> values;
  std::vector<core::TermId> terms;
};

struct BooleanUnitAssignments {
  std::unordered_map<core::TermId, bool> values;
  std::unordered_set<core::TermId> conflicts;
};

struct ForcedAtomAssignments {
  std::unordered_map<core::TermId, bool> values;
  std::unordered_set<core::TermId> conflicts;
  std::vector<std::pair<core::TermId, bool>> units;
  std::vector<std::pair<core::TermId, core::TermId>> equalities;
  std::vector<std::pair<core::TermId, core::TermId>> disequalities;
};

struct UnitEquality {
  core::TermId equality = core::kInvalidTermId;
  core::TermId lhs = core::kInvalidTermId;
  core::TermId rhs = core::kInvalidTermId;
};

struct GroundSubstitutionClasses {
  std::unordered_map<core::TermId, core::TermId> parent;
  std::unordered_set<core::TermId> nodes;

  core::TermId find(core::TermId term) {
    if (!nodes.contains(term)) {
      nodes.insert(term);
    }
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
constexpr std::size_t kMaxCongruenceDefinitionClauses = 10000;
constexpr std::size_t kMaxFullCongruenceApplicationsPerSymbol = 128;
constexpr std::size_t kMaxFiniteDomainEvaluationAssignments = 250000;
constexpr std::size_t kSolvedConstantMaxDefinitionSize = 64;
constexpr std::size_t kSolvedConstantMaxAddedNodes = 200000;

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

bool is_non_bool_constant(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  return term.kind == core::TermKind::Constant && term.sort != terms.symbols().bool_sort();
}

std::size_t tree_term_size(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  std::size_t size = 1;
  for (const core::TermId child : term.children) {
    size += tree_term_size(terms, child);
  }
  return size;
}

void collect_constants_in_term(const core::TermManager& terms, core::TermId root,
                               std::unordered_set<core::TermId>& constants) {
  const core::Term& term = terms.get(root);
  if (term.kind == core::TermKind::Constant) {
    constants.insert(root);
    return;
  }
  for (const core::TermId child : term.children) {
    collect_constants_in_term(terms, child, constants);
  }
}

void count_term_occurrences(const core::TermManager& terms, core::TermId root,
                            std::vector<std::size_t>& occurrences) {
  if (root >= occurrences.size()) {
    occurrences.resize(root + 1);
  }
  ++occurrences[root];
  const core::Term& term = terms.get(root);
  for (const core::TermId child : term.children) {
    count_term_occurrences(terms, child, occurrences);
  }
}

struct SolvedConstantCandidate {
  core::TermId variable = core::kInvalidTermId;
  core::TermId definition = core::kInvalidTermId;
  std::size_t definition_size = 0;
};

std::optional<SolvedConstantCandidate> solved_constant_candidate_from_equality(
    const core::TermManager& terms, core::TermId equality) {
  const core::Term& term = terms.get(equality);
  if (term.kind != core::TermKind::Equality) {
    return std::nullopt;
  }

  const core::TermId lhs = term.children.at(0);
  const core::TermId rhs = term.children.at(1);
  const bool lhs_variable = is_non_bool_constant(terms, lhs);
  const bool rhs_variable = is_non_bool_constant(terms, rhs);
  if (!lhs_variable && !rhs_variable) {
    return std::nullopt;
  }

  const core::TermId variable = lhs_variable ? lhs : rhs;
  const core::TermId definition = lhs_variable ? rhs : lhs;
  return SolvedConstantCandidate{variable, definition, tree_term_size(terms, definition)};
}

void collect_solved_constant_candidates(const core::TermManager& terms, core::TermId assertion,
                                        std::vector<SolvedConstantCandidate>& candidates) {
  const core::Term& term = terms.get(assertion);
  if (term.kind == core::TermKind::And) {
    for (const core::TermId child : term.children) {
      collect_solved_constant_candidates(terms, child, candidates);
    }
    return;
  }
  if (auto candidate = solved_constant_candidate_from_equality(terms, assertion)) {
    candidates.push_back(*candidate);
  }
}

bool is_boolean_unit_atom(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.sort != terms.symbols().bool_sort()) {
    return false;
  }
  switch (term.kind) {
    case core::TermKind::Constant: {
      const core::Symbol& symbol = terms.symbols().get_symbol(term.symbol);
      return !symbol.name.starts_with("$primo_bool_arg_");
    }
    case core::TermKind::Application:
    case core::TermKind::Equality:
    case core::TermKind::BoolValue:
    case core::TermKind::Not:
    case core::TermKind::And:
    case core::TermKind::Or:
    case core::TermKind::Implies:
      return false;
  }
  return false;
}

bool is_forced_atom(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.sort != terms.symbols().bool_sort()) {
    return false;
  }
  switch (term.kind) {
    case core::TermKind::Constant: {
      const core::Symbol& symbol = terms.symbols().get_symbol(term.symbol);
      return !symbol.name.starts_with("$primo_bool_arg_");
    }
    case core::TermKind::Application:
    case core::TermKind::Equality:
      return true;
    case core::TermKind::BoolValue:
    case core::TermKind::Not:
    case core::TermKind::And:
    case core::TermKind::Or:
    case core::TermKind::Implies:
      return false;
  }
  return false;
}

void record_boolean_unit(BooleanUnitAssignments& assignments, core::TermId term, bool value) {
  const auto found = assignments.values.find(term);
  if (found == assignments.values.end()) {
    assignments.values.emplace(term, value);
    return;
  }
  if (found->second != value) {
    assignments.conflicts.insert(term);
  }
}

void record_forced_atom(ForcedAtomAssignments& assignments, const core::TermManager& terms,
                        core::TermId atom, bool value) {
  const auto found = assignments.values.find(atom);
  if (found == assignments.values.end()) {
    assignments.values.emplace(atom, value);
    assignments.units.emplace_back(atom, value);
  } else if (found->second != value) {
    assignments.conflicts.insert(atom);
  }

  const core::Term& node = terms.get(atom);
  if (node.kind != core::TermKind::Equality) {
    return;
  }
  const std::pair<core::TermId, core::TermId> equality{node.children.at(0), node.children.at(1)};
  if (value) {
    assignments.equalities.push_back(equality);
  } else {
    assignments.disequalities.push_back(equality);
  }
}

BooleanUnitAssignments collect_boolean_units(const core::TermManager& terms,
                                             const std::vector<core::TermId>& assertions) {
  std::vector<core::TermId> conjuncts;
  for (const core::TermId assertion : assertions) {
    collect_conjuncts(terms, assertion, conjuncts);
  }

  BooleanUnitAssignments assignments;
  for (const core::TermId conjunct : conjuncts) {
    if (is_boolean_unit_atom(terms, conjunct)) {
      record_boolean_unit(assignments, conjunct, true);
      continue;
    }
    const core::Term& node = terms.get(conjunct);
    if (node.kind != core::TermKind::Not) {
      continue;
    }
    const core::TermId child = node.children.at(0);
    if (is_boolean_unit_atom(terms, child)) {
      record_boolean_unit(assignments, child, false);
    }
  }

  for (const core::TermId conflict : assignments.conflicts) {
    assignments.values.erase(conflict);
  }
  return assignments;
}

ForcedAtomAssignments collect_forced_atoms(core::TermManager& terms,
                                           const std::vector<core::TermId>& assertions) {
  std::vector<core::TermId> conjuncts;
  for (const core::TermId assertion : assertions) {
    collect_conjuncts(terms, assertion, conjuncts);
  }

  ForcedAtomAssignments assignments;
  for (const core::TermId conjunct : conjuncts) {
    if (is_forced_atom(terms, conjunct)) {
      record_forced_atom(assignments, terms, conjunct, true);
      continue;
    }
    const core::Term& node = terms.get(conjunct);
    if (node.kind != core::TermKind::Not) {
      continue;
    }
    const core::TermId child = node.children.at(0);
    if (is_forced_atom(terms, child)) {
      record_forced_atom(assignments, terms, child, false);
    }
  }

  if (assignments.conflicts.empty()) {
    return assignments;
  }

  for (const core::TermId conflict : assignments.conflicts) {
    assignments.values.erase(conflict);
  }
  std::erase_if(assignments.units, [&assignments](const std::pair<core::TermId, bool>& unit) {
    return assignments.conflicts.contains(unit.first);
  });
  auto keep_nonconflicting = [&assignments,
                              &terms](const std::pair<core::TermId, core::TermId>& equality) {
    return !assignments.conflicts.contains(terms.mk_eq(equality.first, equality.second));
  };
  std::erase_if(assignments.equalities,
                [&keep_nonconflicting](const std::pair<core::TermId, core::TermId>& equality) {
                  return !keep_nonconflicting(equality);
                });
  std::erase_if(assignments.disequalities,
                [&keep_nonconflicting](const std::pair<core::TermId, core::TermId>& equality) {
                  return !keep_nonconflicting(equality);
                });
  return assignments;
}

core::TermId simplify_with_boolean_units(core::TermManager& terms, core::TermId term_id,
                                         const std::unordered_map<core::TermId, bool>& assignments,
                                         std::size_t& rewrites) {
  const auto assigned = assignments.find(term_id);
  if (assigned != assignments.end()) {
    ++rewrites;
    return terms.mk_bool(assigned->second);
  }

  const core::Term& term = terms.get(term_id);
  const std::vector<core::TermId>& children = term.children;
  switch (term.kind) {
    case core::TermKind::BoolValue:
    case core::TermKind::Constant:
    case core::TermKind::Application:
      return term_id;
    case core::TermKind::Equality: {
      const core::TermId lhs =
          simplify_with_boolean_units(terms, children.at(0), assignments, rewrites);
      const core::TermId rhs =
          simplify_with_boolean_units(terms, children.at(1), assignments, rewrites);
      return lhs != children.at(0) || rhs != children.at(1) ? terms.mk_eq(lhs, rhs) : term_id;
    }
    case core::TermKind::Not:
      return terms.mk_not(
          simplify_with_boolean_units(terms, children.at(0), assignments, rewrites));
    case core::TermKind::And: {
      std::vector<core::TermId> rewritten;
      rewritten.reserve(children.size());
      for (const core::TermId child : children) {
        rewritten.push_back(simplify_with_boolean_units(terms, child, assignments, rewrites));
      }
      return terms.mk_and(std::move(rewritten));
    }
    case core::TermKind::Or: {
      std::vector<core::TermId> rewritten;
      rewritten.reserve(children.size());
      for (const core::TermId child : children) {
        rewritten.push_back(simplify_with_boolean_units(terms, child, assignments, rewrites));
      }
      return terms.mk_or(std::move(rewritten));
    }
    case core::TermKind::Implies:
      return terms.mk_implies(
          simplify_with_boolean_units(terms, children.at(0), assignments, rewrites),
          simplify_with_boolean_units(terms, children.at(1), assignments, rewrites));
  }
  return term_id;
}

bool is_known_disequal(theory::equality::CongruenceClosure& closure,
                       const std::vector<std::pair<core::TermId, core::TermId>>& disequalities,
                       core::TermId lhs, core::TermId rhs) {
  for (const auto& [diseq_lhs, diseq_rhs] : disequalities) {
    if ((closure.are_equal(lhs, diseq_lhs) && closure.are_equal(rhs, diseq_rhs)) ||
        (closure.are_equal(lhs, diseq_rhs) && closure.are_equal(rhs, diseq_lhs))) {
      return true;
    }
  }
  return false;
}

core::TermId simplify_with_forced_atoms(
    core::TermManager& terms, core::TermId term_id,
    const std::unordered_map<core::TermId, bool>& assignments,
    theory::equality::CongruenceClosure& closure,
    const std::vector<std::pair<core::TermId, core::TermId>>& disequalities,
    std::unordered_map<core::TermId, core::TermId>& memo, std::size_t& rewrites) {
  const auto memoized = memo.find(term_id);
  if (memoized != memo.end()) {
    return memoized->second;
  }

  const auto assigned = assignments.find(term_id);
  if (assigned != assignments.end()) {
    ++rewrites;
    const core::TermId rewritten = terms.mk_bool(assigned->second);
    memo.emplace(term_id, rewritten);
    return rewritten;
  }

  const core::Term& term = terms.get(term_id);
  const core::TermKind kind = term.kind;
  const std::vector<core::TermId> children = term.children;
  core::TermId rewritten = term_id;
  switch (kind) {
    case core::TermKind::BoolValue:
    case core::TermKind::Constant:
    case core::TermKind::Application:
      rewritten = term_id;
      break;
    case core::TermKind::Equality: {
      const core::TermId lhs = simplify_with_forced_atoms(terms, children.at(0), assignments,
                                                          closure, disequalities, memo, rewrites);
      const core::TermId rhs = simplify_with_forced_atoms(terms, children.at(1), assignments,
                                                          closure, disequalities, memo, rewrites);
      if (closure.are_equal(lhs, rhs)) {
        rewritten = terms.mk_bool(true);
      } else if (is_known_disequal(closure, disequalities, lhs, rhs)) {
        rewritten = terms.mk_bool(false);
      } else if (lhs != children.at(0) || rhs != children.at(1)) {
        rewritten = terms.mk_eq(lhs, rhs);
      }
      break;
    }
    case core::TermKind::Not:
      rewritten = terms.mk_not(simplify_with_forced_atoms(terms, children.at(0), assignments,
                                                          closure, disequalities, memo, rewrites));
      break;
    case core::TermKind::And: {
      std::vector<core::TermId> rewritten_children;
      rewritten_children.reserve(children.size());
      for (const core::TermId child : children) {
        rewritten_children.push_back(simplify_with_forced_atoms(terms, child, assignments, closure,
                                                                disequalities, memo, rewrites));
      }
      rewritten = terms.mk_and(std::move(rewritten_children));
      break;
    }
    case core::TermKind::Or: {
      std::vector<core::TermId> rewritten_children;
      rewritten_children.reserve(children.size());
      for (const core::TermId child : children) {
        rewritten_children.push_back(simplify_with_forced_atoms(terms, child, assignments, closure,
                                                                disequalities, memo, rewrites));
      }
      rewritten = terms.mk_or(std::move(rewritten_children));
      break;
    }
    case core::TermKind::Implies:
      rewritten =
          terms.mk_implies(simplify_with_forced_atoms(terms, children.at(0), assignments, closure,
                                                      disequalities, memo, rewrites),
                           simplify_with_forced_atoms(terms, children.at(1), assignments, closure,
                                                      disequalities, memo, rewrites));
      break;
  }
  if (rewritten != term_id) {
    ++rewrites;
  }
  memo.emplace(term_id, rewritten);
  return rewritten;
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

void register_equality_query_terms(const core::TermManager& terms, core::TermId assertion,
                                   theory::equality::CongruenceClosure& closure) {
  std::vector<core::TermId> work{assertion};
  while (!work.empty()) {
    const core::TermId term = work.back();
    work.pop_back();
    const core::Term& node = terms.get(term);
    if (node.kind == core::TermKind::Equality) {
      closure.add_term(node.children.at(0));
      closure.add_term(node.children.at(1));
      continue;
    }
    const std::vector<core::TermId>& children = node.children;
    work.insert(work.end(), children.begin(), children.end());
  }
}

core::TermId simplify_with_congruence(core::TermManager& terms, core::TermId term,
                                      theory::equality::CongruenceClosure& closure) {
  const core::Term& node = terms.get(term);
  const core::TermKind kind = node.kind;
  const std::vector<core::TermId> node_children = node.children;
  switch (kind) {
    case core::TermKind::BoolValue:
    case core::TermKind::Constant:
    case core::TermKind::Application:
      return term;
    case core::TermKind::Equality:
      return closure.are_equal(node_children.at(0), node_children.at(1)) ? terms.mk_bool(true)
                                                                         : term;
    case core::TermKind::Not: {
      const core::TermId child = simplify_with_congruence(terms, node_children.at(0), closure);
      return terms.mk_not(child);
    }
    case core::TermKind::And: {
      std::vector<core::TermId> children;
      children.reserve(node_children.size());
      for (const core::TermId child : node_children) {
        children.push_back(simplify_with_congruence(terms, child, closure));
      }
      return terms.mk_and(std::move(children));
    }
    case core::TermKind::Or: {
      std::vector<core::TermId> children;
      children.reserve(node_children.size());
      for (const core::TermId child : node_children) {
        children.push_back(simplify_with_congruence(terms, child, closure));
      }
      return terms.mk_or(std::move(children));
    }
    case core::TermKind::Implies:
      return terms.mk_implies(simplify_with_congruence(terms, node_children.at(0), closure),
                              simplify_with_congruence(terms, node_children.at(1), closure));
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

std::unordered_set<core::TermId> finite_domain_value_constants(
    const core::TermManager& terms,
    const std::unordered_set<std::pair<core::TermId, core::TermId>, PairHash>& disequalities) {
  std::unordered_set<core::TermId> values;
  for (const auto& [lhs, rhs] : disequalities) {
    const core::Term& lhs_term = terms.get(lhs);
    const core::Term& rhs_term = terms.get(rhs);
    if (lhs_term.kind == core::TermKind::Constant && lhs_term.sort != terms.symbols().bool_sort()) {
      values.insert(lhs);
    }
    if (rhs_term.kind == core::TermKind::Constant && rhs_term.sort != terms.symbols().bool_sort()) {
      values.insert(rhs);
    }
  }
  return values;
}

void add_observed_value_equality(
    const std::unordered_set<core::TermId>& values, core::TermId lhs, core::TermId rhs,
    std::unordered_map<core::TermId, std::vector<core::TermId>>& term_values) {
  if (values.contains(rhs) && !values.contains(lhs) && lhs != rhs) {
    term_values[lhs].push_back(rhs);
  }
  if (values.contains(lhs) && !values.contains(rhs) && lhs != rhs) {
    term_values[rhs].push_back(lhs);
  }
}

std::unordered_map<core::TermId, std::vector<core::TermId>> collect_observed_value_equalities(
    const core::TermManager& terms, const std::vector<core::TermId>& assertions,
    const std::unordered_set<core::TermId>& values) {
  std::unordered_map<core::TermId, std::vector<core::TermId>> term_values;
  std::unordered_set<core::TermId> seen;
  std::vector<core::TermId> work = assertions;
  while (!work.empty()) {
    const core::TermId term_id = work.back();
    work.pop_back();
    if (!seen.insert(term_id).second) {
      continue;
    }
    const core::Term& term = terms.get(term_id);
    if (term.kind == core::TermKind::Equality) {
      add_observed_value_equality(values, term.children.at(0), term.children.at(1), term_values);
    }
    work.insert(work.end(), term.children.begin(), term.children.end());
  }

  for (auto& [term, observed_values] : term_values) {
    (void)term;
    std::sort(observed_values.begin(), observed_values.end());
    observed_values.erase(std::unique(observed_values.begin(), observed_values.end()),
                          observed_values.end());
  }
  return term_values;
}

std::vector<core::TermId> observed_value_at_most_one_clauses(
    core::TermManager& terms, core::TermId term, const std::vector<core::TermId>& values,
    const std::unordered_set<std::pair<core::TermId, core::TermId>, PairHash>& disequalities) {
  std::vector<core::TermId> clauses;
  for (std::size_t i = 0; i < values.size(); ++i) {
    for (std::size_t j = i + 1; j < values.size(); ++j) {
      if (!disequalities.contains(ordered_pair(values[i], values[j]))) {
        continue;
      }
      clauses.push_back(terms.mk_or({terms.mk_not(terms.mk_eq(term, values[i])),
                                     terms.mk_not(terms.mk_eq(term, values[j]))}));
    }
  }
  return clauses;
}

std::vector<core::TermId> finite_domain_distinct_endpoint_clauses(
    core::TermManager& terms, core::TermId lhs, core::TermId rhs,
    const std::vector<core::TermId>& values) {
  std::vector<core::TermId> clauses;
  clauses.reserve(values.size());
  for (const core::TermId value : values) {
    clauses.push_back(terms.mk_or(
        {terms.mk_not(terms.mk_eq(lhs, value)), terms.mk_not(terms.mk_eq(rhs, value))}));
  }
  return clauses;
}

std::vector<core::TermId> collect_used_values(const core::TermManager& terms, core::TermId root,
                                              const std::vector<core::TermId>& values) {
  const std::unordered_set<core::TermId> value_set(values.begin(), values.end());
  std::unordered_set<core::TermId> used;
  std::vector<core::TermId> work{root};
  while (!work.empty()) {
    const core::TermId term_id = work.back();
    work.pop_back();
    if (value_set.contains(term_id)) {
      used.insert(term_id);
    }
    const std::vector<core::TermId>& children = terms.get(term_id).children;
    work.insert(work.end(), children.begin(), children.end());
  }

  std::vector<core::TermId> ordered;
  ordered.reserve(values.size());
  for (const core::TermId value : values) {
    if (used.contains(value)) {
      ordered.push_back(value);
    }
  }
  return ordered;
}

std::unordered_map<core::TermId, std::size_t> count_symmetry_term_occurrences(
    const core::TermManager& terms, const std::vector<core::TermId>& assertions,
    const std::vector<SymmetryTerm>& candidates) {
  std::unordered_set<core::TermId> candidate_terms;
  candidate_terms.reserve(candidates.size());
  for (const SymmetryTerm& candidate : candidates) {
    candidate_terms.insert(candidate.term);
  }

  std::unordered_map<core::TermId, std::size_t> occurrences;
  occurrences.reserve(candidates.size());
  std::vector<core::TermId> work = assertions;
  while (!work.empty()) {
    const core::TermId term_id = work.back();
    work.pop_back();
    if (candidate_terms.contains(term_id)) {
      ++occurrences[term_id];
    }
    const std::vector<core::TermId>& children = terms.get(term_id).children;
    work.insert(work.end(), children.begin(), children.end());
  }
  return occurrences;
}

void annotate_symmetry_terms(const core::TermManager& terms,
                             const std::vector<core::TermId>& assertions, SymmetryGroup& group) {
  const std::unordered_map<core::TermId, std::size_t> occurrences =
      count_symmetry_term_occurrences(terms, assertions, group.terms);
  for (SymmetryTerm& term : group.terms) {
    term.used_values = collect_used_values(terms, term.term, group.values);
    if (const auto found = occurrences.find(term.term); found != occurrences.end()) {
      term.occurrences = found->second;
    }
  }
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
      SymmetryTerm symmetry_term;
      symmetry_term.term = term;
      group->terms.push_back(std::move(symmetry_term));
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

std::vector<std::pair<core::TermId, core::TermId>> collect_observed_equality_pairs(
    const core::TermManager& terms, const std::vector<core::TermId>& assertions) {
  std::vector<std::pair<core::TermId, core::TermId>> equalities;
  std::unordered_set<core::TermId> seen;
  std::vector<core::TermId> work = assertions;
  while (!work.empty()) {
    const core::TermId term_id = work.back();
    work.pop_back();
    if (!seen.insert(term_id).second) {
      continue;
    }
    const core::Term& term = terms.get(term_id);
    if (term.kind == core::TermKind::Equality) {
      const core::TermId lhs = term.children.at(0);
      const core::TermId rhs = term.children.at(1);
      if (lhs != rhs) {
        equalities.push_back(ordered_pair(lhs, rhs));
      }
    }
    work.insert(work.end(), term.children.begin(), term.children.end());
  }
  std::sort(equalities.begin(), equalities.end());
  equalities.erase(std::unique(equalities.begin(), equalities.end()), equalities.end());
  return equalities;
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

void collect_unit_equality_terms(const core::TermManager& terms, core::TermId assertion,
                                 std::vector<UnitEquality>& equalities) {
  std::vector<core::TermId> work{assertion};
  while (!work.empty()) {
    const core::TermId term = work.back();
    work.pop_back();
    const core::Term& node = terms.get(term);
    if (node.kind == core::TermKind::Equality) {
      equalities.push_back(UnitEquality{term, node.children.at(0), node.children.at(1)});
      continue;
    }
    if (node.kind == core::TermKind::And) {
      work.insert(work.end(), node.children.begin(), node.children.end());
    }
  }
}

core::TermId choose_ground_substitution_representative(const core::TermManager& terms,
                                                       const std::vector<core::TermId>& members) {
  core::TermId representative = members.front();
  bool representative_is_constant = terms.get(representative).kind == core::TermKind::Constant;
  for (const core::TermId candidate : members) {
    const bool candidate_is_constant = terms.get(candidate).kind == core::TermKind::Constant;
    if (candidate_is_constant && !representative_is_constant) {
      representative = candidate;
      representative_is_constant = true;
      continue;
    }
    if (candidate_is_constant == representative_is_constant && candidate < representative) {
      representative = candidate;
    }
  }
  return representative;
}

std::unordered_map<core::TermId, core::TermId> build_ground_substitution_map(
    const core::TermManager& terms, const std::vector<UnitEquality>& equalities,
    std::size_t* units) {
  GroundSubstitutionClasses classes;
  const core::SortId bool_sort = terms.symbols().bool_sort();
  for (const UnitEquality& equality : equalities) {
    if (terms.get(equality.lhs).sort == bool_sort || terms.get(equality.rhs).sort == bool_sort) {
      continue;
    }
    classes.unite(equality.lhs, equality.rhs);
    ++(*units);
  }

  std::unordered_map<core::TermId, std::vector<core::TermId>> members_by_root;
  for (const core::TermId node : classes.nodes) {
    members_by_root[classes.find(node)].push_back(node);
  }

  std::unordered_map<core::TermId, core::TermId> replacements;
  for (auto& [root, members] : members_by_root) {
    (void)root;
    if (members.size() < 2) {
      continue;
    }
    const core::TermId representative = choose_ground_substitution_representative(terms, members);
    for (const core::TermId member : members) {
      replacements[member] = representative;
    }
  }
  return replacements;
}

core::TermId replace_with_ground_unit_representative(
    const core::TermManager& terms, core::TermId term,
    const std::unordered_map<core::TermId, core::TermId>& replacements, std::size_t& rewrites) {
  if (terms.get(term).sort == terms.symbols().bool_sort()) {
    return term;
  }
  const auto found = replacements.find(term);
  if (found == replacements.end() || found->second == term) {
    return term;
  }
  ++rewrites;
  return found->second;
}

core::TermId rewrite_with_ground_unit_equalities(
    core::TermManager& terms, core::TermId term,
    const std::unordered_map<core::TermId, core::TermId>& replacements,
    std::unordered_map<core::TermId, core::TermId>& memo, std::size_t& rewrites) {
  if (const auto found = memo.find(term); found != memo.end()) {
    return found->second;
  }

  const core::Term& node = terms.get(term);
  const std::vector<core::TermId> node_children = node.children;
  core::TermId rewritten = term;
  switch (node.kind) {
    case core::TermKind::BoolValue:
    case core::TermKind::Constant:
      break;
    case core::TermKind::Application: {
      std::vector<core::TermId> children;
      children.reserve(node_children.size());
      bool changed = false;
      for (const core::TermId child : node_children) {
        const core::TermId rewritten_child =
            rewrite_with_ground_unit_equalities(terms, child, replacements, memo, rewrites);
        changed = changed || rewritten_child != child;
        children.push_back(rewritten_child);
      }
      if (changed) {
        rewritten = terms.mk_app(node.symbol, std::move(children));
      }
      break;
    }
    case core::TermKind::Equality:
      rewritten = terms.mk_eq(rewrite_with_ground_unit_equalities(terms, node_children.at(0),
                                                                  replacements, memo, rewrites),
                              rewrite_with_ground_unit_equalities(terms, node_children.at(1),
                                                                  replacements, memo, rewrites));
      break;
    case core::TermKind::Not:
      rewritten = terms.mk_not(rewrite_with_ground_unit_equalities(terms, node_children.at(0),
                                                                   replacements, memo, rewrites));
      break;
    case core::TermKind::And: {
      std::vector<core::TermId> children;
      children.reserve(node_children.size());
      for (const core::TermId child : node_children) {
        children.push_back(
            rewrite_with_ground_unit_equalities(terms, child, replacements, memo, rewrites));
      }
      rewritten = terms.mk_and(std::move(children));
      break;
    }
    case core::TermKind::Or: {
      std::vector<core::TermId> children;
      children.reserve(node_children.size());
      for (const core::TermId child : node_children) {
        children.push_back(
            rewrite_with_ground_unit_equalities(terms, child, replacements, memo, rewrites));
      }
      rewritten = terms.mk_or(std::move(children));
      break;
    }
    case core::TermKind::Implies:
      rewritten = terms.mk_implies(rewrite_with_ground_unit_equalities(
                                       terms, node_children.at(0), replacements, memo, rewrites),
                                   rewrite_with_ground_unit_equalities(
                                       terms, node_children.at(1), replacements, memo, rewrites));
      break;
  }

  rewritten = replace_with_ground_unit_representative(terms, rewritten, replacements, rewrites);
  memo[term] = rewritten;
  return rewritten;
}

core::TermId rewrite_with_solved_constants(
    core::TermManager& terms, core::TermId term_id,
    const std::unordered_map<core::TermId, core::TermId>& replacements,
    std::unordered_map<core::TermId, core::TermId>& memo, std::size_t& rewrites) {
  if (const auto found = memo.find(term_id); found != memo.end()) {
    return found->second;
  }

  if (const auto replacement = replacements.find(term_id); replacement != replacements.end()) {
    const core::TermId rewritten =
        rewrite_with_solved_constants(terms, replacement->second, replacements, memo, rewrites);
    if (rewritten != term_id) {
      ++rewrites;
    }
    memo.emplace(term_id, rewritten);
    return rewritten;
  }

  const core::Term& term = terms.get(term_id);
  const std::vector<core::TermId> children = term.children;
  core::TermId rewritten = term_id;
  switch (term.kind) {
    case core::TermKind::BoolValue:
    case core::TermKind::Constant:
      break;
    case core::TermKind::Application: {
      std::vector<core::TermId> rewritten_children;
      rewritten_children.reserve(children.size());
      bool changed = false;
      for (const core::TermId child : children) {
        const core::TermId rewritten_child =
            rewrite_with_solved_constants(terms, child, replacements, memo, rewrites);
        changed = changed || rewritten_child != child;
        rewritten_children.push_back(rewritten_child);
      }
      if (changed) {
        rewritten = terms.mk_app(term.symbol, std::move(rewritten_children));
      }
      break;
    }
    case core::TermKind::Equality:
      rewritten = terms.mk_eq(
          rewrite_with_solved_constants(terms, children.at(0), replacements, memo, rewrites),
          rewrite_with_solved_constants(terms, children.at(1), replacements, memo, rewrites));
      break;
    case core::TermKind::Not:
      rewritten = terms.mk_not(
          rewrite_with_solved_constants(terms, children.at(0), replacements, memo, rewrites));
      break;
    case core::TermKind::And: {
      std::vector<core::TermId> rewritten_children;
      rewritten_children.reserve(children.size());
      for (const core::TermId child : children) {
        rewritten_children.push_back(
            rewrite_with_solved_constants(terms, child, replacements, memo, rewrites));
      }
      rewritten = terms.mk_and(std::move(rewritten_children));
      break;
    }
    case core::TermKind::Or: {
      std::vector<core::TermId> rewritten_children;
      rewritten_children.reserve(children.size());
      for (const core::TermId child : children) {
        rewritten_children.push_back(
            rewrite_with_solved_constants(terms, child, replacements, memo, rewrites));
      }
      rewritten = terms.mk_or(std::move(rewritten_children));
      break;
    }
    case core::TermKind::Implies:
      rewritten = terms.mk_implies(
          rewrite_with_solved_constants(terms, children.at(0), replacements, memo, rewrites),
          rewrite_with_solved_constants(terms, children.at(1), replacements, memo, rewrites));
      break;
  }
  if (rewritten != term_id) {
    ++rewrites;
  }
  memo.emplace(term_id, rewritten);
  return rewritten;
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
  const core::TermKind kind = node.kind;
  const std::vector<core::TermId> node_children = node.children;
  switch (kind) {
    case core::TermKind::BoolValue:
    case core::TermKind::Constant:
    case core::TermKind::Application:
      return term;
    case core::TermKind::Equality: {
      bool cutoff = false;
      if (state.always_equal(node_children.at(0), node_children.at(1), &cutoff)) {
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
          simplify_with_finite_domain_evaluation(terms, node_children.at(0), state, result));
    case core::TermKind::And: {
      std::vector<core::TermId> children;
      children.reserve(node_children.size());
      for (const core::TermId child : node_children) {
        children.push_back(simplify_with_finite_domain_evaluation(terms, child, state, result));
      }
      return terms.mk_and(std::move(children));
    }
    case core::TermKind::Or: {
      std::vector<core::TermId> children;
      children.reserve(node_children.size());
      for (const core::TermId child : node_children) {
        children.push_back(simplify_with_finite_domain_evaluation(terms, child, state, result));
      }
      return terms.mk_or(std::move(children));
    }
    case core::TermKind::Implies:
      return terms.mk_implies(
          simplify_with_finite_domain_evaluation(terms, node_children.at(0), state, result),
          simplify_with_finite_domain_evaluation(terms, node_children.at(1), state, result));
  }
  return term;
}

core::TermId congruence_definition_clause(core::TermManager& terms, core::TermId lhs,
                                          core::TermId rhs) {
  const core::Term& lhs_term = terms.get(lhs);
  const core::Term& rhs_term = terms.get(rhs);
  const std::vector<core::TermId> lhs_children = lhs_term.children;
  const std::vector<core::TermId> rhs_children = rhs_term.children;
  std::vector<core::TermId> literals;
  literals.reserve(lhs_children.size() + 1);
  for (std::size_t i = 0; i < lhs_children.size(); ++i) {
    literals.push_back(terms.mk_not(terms.mk_eq(lhs_children[i], rhs_children[i])));
  }
  literals.push_back(terms.mk_eq(lhs, rhs));
  return terms.mk_or(std::move(literals));
}

bool application_is_congruence_candidate(
    const core::TermManager& terms, core::TermId application,
    const std::unordered_set<core::TermId>& finite_domain_terms) {
  if (finite_domain_terms.contains(application)) {
    return true;
  }
  for (const core::TermId child : terms.get(application).children) {
    if (finite_domain_terms.contains(child)) {
      return true;
    }
  }
  return false;
}

void add_unique_symmetry_constraint(std::unordered_set<core::TermId>& emitted,
                                    UfSymmetryBreakingResult& result, core::TermId constraint) {
  if (emitted.insert(constraint).second) {
    result.constraints.push_back(constraint);
  }
}

void add_paper_symmetry_constraints(core::TermManager& terms, const SymmetryGroup& group,
                                    std::unordered_set<core::TermId>& emitted,
                                    UfSymmetryBreakingResult& result) {
  // Déharbe et al., Algorithm 1, tracks constants already used by selected
  // terms. A moving term may be restricted only after preserving the constants
  // it syntactically contains; otherwise a permutation could move the term.
  std::unordered_set<core::TermId> used_constants;
  for (const SymmetryTerm& term : group.terms) {
    for (const core::TermId used_value : term.used_values) {
      used_constants.insert(used_value);
    }
    for (const core::TermId value : group.values) {
      if (!used_constants.contains(value)) {
        used_constants.insert(value);
        break;
      }
    }
    if (used_constants.size() == group.values.size()) {
      continue;
    }
    for (const core::TermId value : group.values) {
      if (!used_constants.contains(value)) {
        add_unique_symmetry_constraint(emitted, result,
                                       terms.mk_not(terms.mk_eq(term.term, value)));
      }
    }
  }
}

std::vector<SymmetryTerm> fixed_symmetry_terms(const SymmetryGroup& group) {
  std::vector<SymmetryTerm> fixed;
  for (const SymmetryTerm& term : group.terms) {
    if (term.used_values.empty()) {
      fixed.push_back(term);
    }
  }
  std::sort(fixed.begin(), fixed.end(),
            [](const SymmetryTerm& lhs, const SymmetryTerm& rhs) { return lhs.term < rhs.term; });
  return fixed;
}

void add_value_precedence_constraints(core::TermManager& terms,
                                      const std::vector<SymmetryTerm>& fixed_terms,
                                      const std::vector<core::TermId>& values,
                                      std::unordered_set<core::TermId>& emitted,
                                      UfSymmetryBreakingResult& result) {
  if (values.empty()) {
    return;
  }
  for (std::size_t term_index = 0; term_index < fixed_terms.size(); ++term_index) {
    for (std::size_t value_index = term_index + 1; value_index < values.size(); ++value_index) {
      add_unique_symmetry_constraint(
          emitted, result,
          terms.mk_not(terms.mk_eq(fixed_terms[term_index].term, values[value_index])));
    }

    const std::size_t max_guarded_value = std::min(term_index, values.size() - 1);
    for (std::size_t value_index = 1; value_index <= max_guarded_value; ++value_index) {
      std::vector<core::TermId> clause;
      clause.reserve(term_index + 1);
      clause.push_back(
          terms.mk_not(terms.mk_eq(fixed_terms[term_index].term, values[value_index])));
      for (std::size_t prior_index = 0; prior_index < term_index; ++prior_index) {
        clause.push_back(terms.mk_eq(fixed_terms[prior_index].term, values[value_index - 1]));
      }
      add_unique_symmetry_constraint(emitted, result, terms.mk_or(std::move(clause)));
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
    register_equality_query_terms(terms, assertion, closure);
  }
  closure.saturate();

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

GroundEqualitySubstitutionResult substitute_ground_unit_equalities(
    core::TermManager& terms, const std::vector<core::TermId>& assertions) {
  // CVC4's nonclausal simplification architecture motivates this bounded
  // subset: unit equations asserted by the Boolean structure are kept as facts,
  // while the rest of the current assertion DAG is rebuilt modulo those facts.
  // Primo only collects top-level/conjunctive positive non-Bool equalities here.
  GroundEqualitySubstitutionResult result;
  std::vector<UnitEquality> unit_equalities;
  for (const core::TermId assertion : assertions) {
    collect_unit_equality_terms(terms, assertion, unit_equalities);
  }
  const std::unordered_map<core::TermId, core::TermId> replacements =
      build_ground_substitution_map(terms, unit_equalities, &result.units);
  if (replacements.empty()) {
    result.assertions = assertions;
    return result;
  }

  result.assertions.reserve(assertions.size() + unit_equalities.size());
  std::unordered_set<core::TermId> emitted;
  for (const UnitEquality& equality : unit_equalities) {
    if (emitted.insert(equality.equality).second) {
      result.assertions.push_back(equality.equality);
    }
  }

  std::unordered_map<core::TermId, core::TermId> memo;
  for (const core::TermId assertion : assertions) {
    const core::TermId rewritten =
        rewrite_with_ground_unit_equalities(terms, assertion, replacements, memo, result.rewrites);
    const core::Term& node = terms.get(rewritten);
    if (node.kind == core::TermKind::BoolValue && node.bool_value) {
      continue;
    }
    if (emitted.insert(rewritten).second) {
      result.assertions.push_back(rewritten);
    }
  }
  return result;
}

SolvedConstantSubstitutionResult substitute_solved_constants(
    core::TermManager& terms, const std::vector<core::TermId>& assertions) {
  // This is satisfiability-preserving definitional elimination for free
  // uninterpreted constants: a unit equality x = t may replace x when x does
  // not occur in t. It is deliberately smaller than a full solve-eqs tactic.
  SolvedConstantSubstitutionResult result;
  result.assertions = assertions;

  std::vector<SolvedConstantCandidate> candidates;
  for (const core::TermId assertion : assertions) {
    collect_solved_constant_candidates(terms, assertion, candidates);
  }

  std::vector<std::size_t> occurrences(terms.size());
  for (const core::TermId assertion : assertions) {
    count_term_occurrences(terms, assertion, occurrences);
  }

  result.candidates = candidates.size();
  if (candidates.empty()) {
    return result;
  }

  std::unordered_set<core::TermId> candidate_variables;
  for (const SolvedConstantCandidate& candidate : candidates) {
    candidate_variables.insert(candidate.variable);
  }

  std::unordered_map<core::TermId, core::TermId> replacements;
  for (const SolvedConstantCandidate& candidate : candidates) {
    std::unordered_set<core::TermId> definition_constants;
    collect_constants_in_term(terms, candidate.definition, definition_constants);
    if (definition_constants.contains(candidate.variable)) {
      ++result.rejected_cycles;
      continue;
    }
    bool depends_on_pending_definition = false;
    for (const core::TermId variable : candidate_variables) {
      if (variable != candidate.variable && definition_constants.contains(variable)) {
        depends_on_pending_definition = true;
        break;
      }
    }
    if (depends_on_pending_definition) {
      ++result.rejected_cycles;
      continue;
    }
    const std::size_t occurrences_of_variable =
        candidate.variable < occurrences.size() ? occurrences[candidate.variable] : 0;
    const std::size_t added_nodes =
        occurrences_of_variable > 0 ? occurrences_of_variable * candidate.definition_size : 0;
    if (candidate.definition_size > kSolvedConstantMaxDefinitionSize ||
        added_nodes > kSolvedConstantMaxAddedNodes) {
      ++result.rejected_blowups;
      continue;
    }
    replacements.emplace(candidate.variable, candidate.definition);
  }

  if (replacements.empty()) {
    return result;
  }

  result.accepted = replacements.size();
  result.assertions.clear();
  result.assertions.reserve(assertions.size());
  std::unordered_map<core::TermId, core::TermId> memo;
  for (const core::TermId assertion : assertions) {
    result.assertions.push_back(
        rewrite_with_solved_constants(terms, assertion, replacements, memo, result.rewrites));
  }
  return result;
}

BooleanUnitPropagationResult propagate_boolean_units(core::TermManager& terms,
                                                     const std::vector<core::TermId>& assertions) {
  BooleanUnitPropagationResult result;
  const BooleanUnitAssignments assignments = collect_boolean_units(terms, assertions);
  result.units = assignments.values.size();
  result.assertions.reserve(assertions.size());
  std::unordered_set<core::TermId> emitted;
  for (const auto& [term, value] : assignments.values) {
    const core::TermId unit = value ? term : terms.mk_not(term);
    if (emitted.insert(unit).second) {
      result.assertions.push_back(unit);
    }
  }
  for (const core::TermId assertion : assertions) {
    const core::TermId simplified =
        simplify_with_boolean_units(terms, assertion, assignments.values, result.rewrites);
    const core::Term& node = terms.get(simplified);
    if (node.kind == core::TermKind::BoolValue && node.bool_value) {
      continue;
    }
    if (emitted.insert(simplified).second) {
      result.assertions.push_back(simplified);
    }
  }
  return result;
}

ForcedAtomPropagationResult propagate_forced_atoms(core::TermManager& terms,
                                                   const std::vector<core::TermId>& assertions) {
  // This is SAT-style forced-literal simplification over the current assertion
  // conjunction. It is deliberately nonclausal: top-level atom facts are kept as
  // assertions, while compound formulas are rebuilt under those facts before
  // Boolean abstraction. Equality facts additionally seed a congruence closure
  // and negative equality facts are used as known disequalities.
  ForcedAtomPropagationResult result;
  const ForcedAtomAssignments assignments = collect_forced_atoms(terms, assertions);
  result.units = assignments.units.size();
  result.disequalities = assignments.disequalities.size();
  if (assignments.values.empty() && assignments.equalities.empty() &&
      assignments.disequalities.empty()) {
    result.assertions = assertions;
    return result;
  }

  if (assignments.equalities.empty() && assignments.disequalities.empty()) {
    result.assertions.reserve(assertions.size() + assignments.units.size());
    std::unordered_set<core::TermId> emitted;
    for (const auto& [term, value] : assignments.units) {
      const core::TermId unit = value ? term : terms.mk_not(term);
      if (emitted.insert(unit).second) {
        result.assertions.push_back(unit);
      }
    }
    for (const core::TermId assertion : assertions) {
      const core::TermId simplified =
          simplify_with_boolean_units(terms, assertion, assignments.values, result.rewrites);
      const core::Term& node = terms.get(simplified);
      if (node.kind == core::TermKind::BoolValue && node.bool_value) {
        continue;
      }
      if (emitted.insert(simplified).second) {
        result.assertions.push_back(simplified);
      }
    }
    return result;
  }

  theory::equality::CongruenceClosure closure(terms);
  for (const auto& [lhs, rhs] : assignments.equalities) {
    closure.assert_equal(lhs, rhs);
  }
  for (const auto& [lhs, rhs] : assignments.disequalities) {
    closure.add_term(lhs);
    closure.add_term(rhs);
  }
  for (const core::TermId assertion : assertions) {
    register_equality_query_terms(terms, assertion, closure);
  }
  closure.saturate();

  result.assertions.reserve(assertions.size() + assignments.units.size());
  std::unordered_set<core::TermId> emitted;
  for (const auto& [term, value] : assignments.units) {
    const core::TermId unit = value ? term : terms.mk_not(term);
    if (emitted.insert(unit).second) {
      result.assertions.push_back(unit);
    }
  }
  for (const auto& [lhs, rhs] : assignments.disequalities) {
    if (closure.are_equal(lhs, rhs) && emitted.insert(terms.mk_bool(false)).second) {
      result.assertions.push_back(terms.mk_bool(false));
    }
  }

  std::unordered_map<core::TermId, core::TermId> memo;
  for (const core::TermId assertion : assertions) {
    const core::TermId simplified =
        simplify_with_forced_atoms(terms, assertion, assignments.values, closure,
                                   assignments.disequalities, memo, result.rewrites);
    const core::Term& node = terms.get(simplified);
    if (node.kind == core::TermKind::BoolValue && node.bool_value) {
      continue;
    }
    if (emitted.insert(simplified).second) {
      result.assertions.push_back(simplified);
    }
  }
  return result;
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
  for (const auto& [lhs, rhs] : disequalities) {
    const auto lhs_choice = choices.find(lhs);
    const auto rhs_choice = choices.find(rhs);
    if (lhs_choice == choices.end() || rhs_choice == choices.end() ||
        lhs_choice->second.values != rhs_choice->second.values) {
      continue;
    }
    const std::vector<core::TermId> clauses =
        finite_domain_distinct_endpoint_clauses(terms_, lhs, rhs, lhs_choice->second.values);
    for (const core::TermId clause : clauses) {
      if (emitted.insert(clause).second) {
        result.clauses.push_back(clause);
      }
    }
  }
  const std::unordered_set<core::TermId> value_constants =
      finite_domain_value_constants(terms_, disequalities);
  const auto observed_equalities =
      collect_observed_value_equalities(terms_, assertions, value_constants);
  for (const auto& [term, values] : observed_equalities) {
    if (values.size() < 2) {
      continue;
    }
    const std::vector<core::TermId> clauses =
        observed_value_at_most_one_clauses(terms_, term, values, disequalities);
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
    const std::vector<core::TermId>& assertions, FiniteDomainEqualityDefinitionsOptions options) {
  // Bradley and Manna's ground EUF setting justifies these finite-domain
  // definition clauses when every involved term has an explicit, pairwise
  // disequal choice domain. They expose (= s t) through the SAT-visible choices
  // (= s c_i) and (= t c_i), without adding fresh propositional variables.
  FiniteDomainEqualityDefinitionsResult result;
  const auto disequalities = collect_disequalities(terms_, assertions);
  const auto choices = collect_finite_domain_choices(terms_, assertions, disequalities);
  const std::vector<std::pair<core::TermId, core::TermId>> observed_equalities =
      collect_observed_equality_pairs(terms_, assertions);
  std::unordered_set<core::TermId> emitted(assertions.begin(), assertions.end());
  for (const auto& [lhs, rhs] : observed_equalities) {
    const auto lhs_choice = choices.find(lhs);
    const auto rhs_choice = choices.find(rhs);
    if (lhs_choice == choices.end() || rhs_choice == choices.end() ||
        lhs_choice->second.values != rhs_choice->second.values) {
      continue;
    }
    ++result.candidate_term_pairs;
    const std::size_t predicted_clauses = lhs_choice->second.values.size() * 3U;
    result.predicted_clauses += predicted_clauses;
    if (options.max_domain_size > 0 && lhs_choice->second.values.size() > options.max_domain_size) {
      ++result.rejected_domain_size;
      continue;
    }
    if (options.max_clauses > 0 &&
        result.clauses.size() + predicted_clauses > options.max_clauses) {
      ++result.rejected_clause_limit;
      continue;
    }
    const std::vector<core::TermId> clauses =
        finite_domain_equality_definition_clauses(terms_, lhs, rhs, lhs_choice->second.values);
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
  const auto disequalities = collect_disequalities(terms_, assertions);
  const auto choices = collect_finite_domain_choices(terms_, assertions, disequalities);
  std::unordered_set<core::TermId> finite_domain_terms;
  for (const auto& [term, choice] : choices) {
    finite_domain_terms.insert(term);
    finite_domain_terms.insert(choice.values.begin(), choice.values.end());
  }

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
    if (applications.size() > kMaxFullCongruenceApplicationsPerSymbol) {
      applications.erase(std::remove_if(applications.begin(), applications.end(),
                                        [&](const core::TermId application) {
                                          return !application_is_congruence_candidate(
                                              terms_, application, finite_domain_terms);
                                        }),
                         applications.end());
      if (applications.size() < 2) {
        ++result.skipped_groups;
        continue;
      }
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
    const std::vector<core::TermId>& assertions, UfSymmetryBreakingOptions options) {
  UfSymmetryBreakingResult result;
  const auto disequalities = collect_disequalities(terms_, assertions);
  const auto choices = collect_finite_domain_choices(terms_, assertions, disequalities);
  std::vector<SymmetryGroup> groups = build_groups(terms_, choices);
  std::unordered_set<core::TermId> emitted(assertions.begin(), assertions.end());

  for (SymmetryGroup& group : groups) {
    annotate_symmetry_terms(terms_, assertions, group);
    if (group.terms.empty()) {
      ++result.rejected_noninvariant;
      continue;
    }
    if (!formulas_are_invariant_under_value_permutations(terms_, assertions, group.values)) {
      ++result.rejected_noninvariant;
      continue;
    }

    ++result.accepted_sets;
    result.values += group.values.size();
    std::vector<SymmetryTerm> fixed_terms = fixed_symmetry_terms(group);
    if (!fixed_terms.empty() && options.value_precedence) {
      const std::size_t constraints_before = result.constraints.size();
      result.terms += fixed_terms.size();
      add_value_precedence_constraints(terms_, fixed_terms, group.values, emitted, result);
      result.value_precedence_terms += fixed_terms.size();
      result.value_precedence_constraints += result.constraints.size() - constraints_before;
      continue;
    }

    std::sort(group.terms.begin(), group.terms.end(),
              [](const SymmetryTerm& lhs, const SymmetryTerm& rhs) {
                const std::size_t lhs_denominator = lhs.used_values.size() + 1;
                const std::size_t rhs_denominator = rhs.used_values.size() + 1;
                const std::size_t lhs_score = lhs.occurrences * rhs_denominator;
                const std::size_t rhs_score = rhs.occurrences * lhs_denominator;
                if (lhs_score != rhs_score) {
                  return lhs_score > rhs_score;
                }
                if (lhs.used_values.size() != rhs.used_values.size()) {
                  return lhs.used_values.size() < rhs.used_values.size();
                }
                return lhs.term < rhs.term;
              });
    result.terms += group.terms.size();
    add_paper_symmetry_constraints(terms_, group, emitted, result);
  }

  return result;
}

}  // namespace primo::smt
