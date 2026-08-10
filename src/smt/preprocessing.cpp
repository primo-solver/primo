#include "smt/preprocessing.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "theory/arithmetic/linear_polynomial.hpp"
#include "theory/arithmetic/lra_atom.hpp"
#include "theory/equality/congruence_stats.hpp"
#include "theory/equality/incremental_congruence_closure.hpp"
#include "util/rational.hpp"

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

struct KnownDisequalityClasses {
  std::unordered_set<std::pair<core::TermId, core::TermId>, PairHash> pairs;
};

struct AdditionalFiniteDomainFacts {
  theory::equality::IncrementalCongruenceClosure closure;
  std::unordered_set<std::pair<core::TermId, core::TermId>, PairHash> disequality_representatives;

  explicit AdditionalFiniteDomainFacts(core::TermManager& terms) : closure(terms) {}
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

using SubtreeOccurrenceMap =
    std::unordered_map<core::TermId, std::vector<std::pair<core::TermId, std::size_t>>>;

const std::vector<std::pair<core::TermId, std::size_t>>& subtree_occurrences(
    const core::TermManager& terms, core::TermId root,
    const std::unordered_set<core::TermId>& candidate_variables, SubtreeOccurrenceMap& memo) {
  struct Visit {
    core::TermId term;
    bool expanded;
  };

  std::vector<Visit> pending{{root, false}};
  while (!pending.empty()) {
    const Visit visit = pending.back();
    pending.pop_back();
    if (memo.contains(visit.term)) {
      continue;
    }

    const core::Term& term = terms.get(visit.term);
    if (!visit.expanded) {
      pending.push_back(Visit{visit.term, true});
      for (auto child = term.children.rbegin(); child != term.children.rend(); ++child) {
        if (!memo.contains(*child)) {
          pending.push_back(Visit{*child, false});
        }
      }
      continue;
    }

    std::unordered_map<core::TermId, std::size_t> counts;
    if (candidate_variables.contains(visit.term)) {
      counts.emplace(visit.term, 1);
    }
    for (const core::TermId child : term.children) {
      for (const auto& [term_id, count] : memo.at(child)) {
        counts[term_id] += count;
      }
    }

    std::vector<std::pair<core::TermId, std::size_t>> flattened;
    flattened.reserve(counts.size());
    for (const auto& [term_id, count] : counts) {
      flattened.emplace_back(term_id, count);
    }
    memo.emplace(visit.term, std::move(flattened));
  }
  return memo.at(root);
}

void count_term_occurrences(const core::TermManager& terms, core::TermId root,
                            std::vector<std::size_t>& occurrences,
                            const std::unordered_set<core::TermId>& candidate_variables,
                            SubtreeOccurrenceMap& subtree_memo) {
  for (const auto& [term_id, count] :
       subtree_occurrences(terms, root, candidate_variables, subtree_memo)) {
    if (term_id >= occurrences.size()) {
      occurrences.resize(term_id + 1);
    }
    occurrences[term_id] += count;
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

// Same top-level/top-level-conjunct scope as collect_solved_constant_
// candidates, but collects every Real-sorted Equality node found (not
// just the bare-variable-shaped ones solved_constant_candidate_from_
// equality recognizes) -- feeds the Gaussian-elimination candidate scan
// below, which can solve equations solved_constant_candidate_from_
// equality can't (no bare-variable side, or only solvable jointly with
// other equations).
void collect_real_equalities(const core::TermManager& terms, core::TermId assertion,
                             std::vector<core::TermId>& equalities) {
  const core::Term& term = terms.get(assertion);
  if (term.kind == core::TermKind::And) {
    for (const core::TermId child : term.children) {
      collect_real_equalities(terms, child, equalities);
    }
    return;
  }
  if (term.kind == core::TermKind::Equality &&
      terms.get(term.children.at(0)).sort == terms.symbols().real_sort()) {
    equalities.push_back(assertion);
  }
}

// Rebuilds a LinearPolynomial as a core::TermId (the direction
// LinearPolynomial itself doesn't provide) -- variables sorted by TermId
// for determinism.
core::TermId polynomial_to_term(core::TermManager& terms,
                                const theory::arithmetic::LinearPolynomial& polynomial) {
  std::vector<core::TermId> variables;
  variables.reserve(polynomial.terms().size());
  for (const auto& [variable, coefficient] : polynomial.terms()) {
    variables.push_back(variable);
  }
  std::sort(variables.begin(), variables.end());

  std::vector<core::TermId> summands;
  summands.reserve(variables.size() + 1);
  for (const core::TermId variable : variables) {
    const util::Rational& coefficient = polynomial.terms().at(variable);
    summands.push_back(coefficient == util::Rational(1) ? variable
                                                        : terms.mk_times(coefficient, variable));
  }
  if (!polynomial.constant().is_zero() || summands.empty()) {
    summands.push_back(terms.mk_rational(polynomial.constant()));
  }
  return summands.size() == 1 ? summands.front() : terms.mk_plus(std::move(summands));
}

struct GaussianEliminationRow {
  std::unordered_map<core::TermId, util::Rational> coefficients;
  util::Rational rhs;
};

// Defensive bounds on the O(equations^2 * variables) elimination cost
// below -- generous enough to matter for real formulas, small enough to
// bound worst-case CPU/memory on an adversarial/fuzzed input. Not
// precisely derived; tunable against real evidence later.
constexpr std::size_t kGaussianEliminationMaxEquations = 128;
constexpr std::size_t kGaussianEliminationMaxVariables = 256;

// Row-reduces (Gauss-Jordan, exact rationals) the system of Real-sorted
// equalities in `assertions` and returns one SolvedConstantCandidate per
// variable that ends up with a pivot row. Full (not just forward)
// elimination: eliminating a variable removes it from every other row,
// including already-pivoted ones, the moment it's chosen as a pivot --
// so no back-substitution step is needed, and (structurally, not just as
// an invariant checked afterward) no candidate this function returns can
// reference another candidate this function returns, since a pivot
// variable is scrubbed from every row but its own before the next pivot
// is chosen.
std::vector<SolvedConstantCandidate> collect_gaussian_elimination_candidates(
    core::TermManager& terms, const std::vector<core::TermId>& assertions) {
  std::vector<SolvedConstantCandidate> result;

  std::vector<core::TermId> equalities;
  for (const core::TermId assertion : assertions) {
    collect_real_equalities(terms, assertion, equalities);
  }
  if (equalities.empty() || equalities.size() > kGaussianEliminationMaxEquations) {
    return result;
  }

  std::vector<GaussianEliminationRow> rows;
  rows.reserve(equalities.size());
  std::unordered_set<core::TermId> all_variables;
  for (const core::TermId equality : equalities) {
    const theory::arithmetic::LraAtom atom = theory::arithmetic::normalize_atom(terms, equality);
    GaussianEliminationRow row;
    row.rhs = atom.rhs;
    row.coefficients.insert(atom.polynomial.terms().begin(), atom.polynomial.terms().end());
    for (const auto& [variable, coefficient] : row.coefficients) {
      all_variables.insert(variable);
    }
    rows.push_back(std::move(row));
  }

  std::vector<core::TermId> eligible_variables;
  for (const core::TermId variable : all_variables) {
    if (is_non_bool_constant(terms, variable)) {
      eligible_variables.push_back(variable);
    }
  }
  if (eligible_variables.size() > kGaussianEliminationMaxVariables) {
    return result;
  }
  std::sort(eligible_variables.begin(), eligible_variables.end());

  std::vector<bool> row_used(rows.size(), false);
  std::unordered_map<core::TermId, std::size_t> pivot_row_of_variable;
  for (const core::TermId variable : eligible_variables) {
    std::optional<std::size_t> pivot_index;
    for (std::size_t i = 0; i < rows.size(); ++i) {
      if (row_used[i]) {
        continue;
      }
      const auto found = rows[i].coefficients.find(variable);
      if (found != rows[i].coefficients.end() && !found->second.is_zero()) {
        pivot_index = i;
        break;
      }
    }
    if (!pivot_index.has_value()) {
      continue;
    }
    row_used[*pivot_index] = true;
    GaussianEliminationRow& pivot_row = rows[*pivot_index];
    const util::Rational pivot_coefficient = pivot_row.coefficients.at(variable);
    for (auto& [pivot_variable, coefficient] : pivot_row.coefficients) {
      coefficient = coefficient / pivot_coefficient;
    }
    pivot_row.rhs = pivot_row.rhs / pivot_coefficient;

    for (std::size_t i = 0; i < rows.size(); ++i) {
      if (i == *pivot_index) {
        continue;
      }
      const auto found = rows[i].coefficients.find(variable);
      if (found == rows[i].coefficients.end() || found->second.is_zero()) {
        continue;
      }
      const util::Rational factor = found->second;
      for (const auto& [pivot_variable, coefficient] : pivot_row.coefficients) {
        if (pivot_variable == variable) {
          continue;
        }
        util::Rational updated = rows[i].coefficients[pivot_variable] - factor * coefficient;
        if (updated.is_zero()) {
          rows[i].coefficients.erase(pivot_variable);
        } else {
          rows[i].coefficients[pivot_variable] = updated;
        }
      }
      rows[i].rhs = rows[i].rhs - factor * pivot_row.rhs;
      rows[i].coefficients.erase(variable);
    }

    pivot_row_of_variable.emplace(variable, *pivot_index);
  }

  for (const auto& [variable, row_index] : pivot_row_of_variable) {
    const GaussianEliminationRow& row = rows[row_index];
    theory::arithmetic::LinearPolynomial definition_polynomial(row.rhs);
    for (const auto& [other_variable, coefficient] : row.coefficients) {
      if (other_variable == variable) {
        continue;
      }
      definition_polynomial.add_term(other_variable, -coefficient);
    }
    const core::TermId definition = polynomial_to_term(terms, definition_polynomial);
    result.push_back(
        SolvedConstantCandidate{variable, definition, tree_term_size(terms, definition)});
  }
  return result;
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
    case core::TermKind::Le:
    case core::TermKind::Lt:
    case core::TermKind::BoolValue:
    case core::TermKind::Not:
    case core::TermKind::And:
    case core::TermKind::Or:
    case core::TermKind::Implies:
      return false;
    case core::TermKind::Rational:
    case core::TermKind::Plus:
    case core::TermKind::Times:
      // Real-sorted; unreachable past the bool_sort() guard above.
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
    case core::TermKind::Le:
    case core::TermKind::Lt:
      return true;
    case core::TermKind::BoolValue:
    case core::TermKind::Not:
    case core::TermKind::And:
    case core::TermKind::Or:
    case core::TermKind::Implies:
      return false;
    case core::TermKind::Rational:
    case core::TermKind::Plus:
    case core::TermKind::Times:
      // Real-sorted; unreachable past the bool_sort() guard above.
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

struct BooleanUnitSimplificationCache {
  struct Entry {
    core::TermId term = 0;
    std::size_t rewrites = 0;
  };

  std::unordered_map<core::TermId, Entry> entries;
  std::size_t hits = 0;
  std::size_t misses = 0;
};

core::TermId simplify_with_boolean_units(core::TermManager& terms, core::TermId term_id,
                                         const std::unordered_map<core::TermId, bool>& assignments,
                                         BooleanUnitSimplificationCache& cache,
                                         std::size_t& rewrites) {
  struct Visit {
    core::TermId term;
    bool expanded;
    std::size_t rewrites_before;
  };

  std::vector<Visit> pending{{term_id, false, 0}};
  while (!pending.empty()) {
    const Visit visit = pending.back();
    pending.pop_back();
    if (!visit.expanded) {
      if (const auto cached = cache.entries.find(visit.term); cached != cache.entries.end()) {
        ++cache.hits;
        rewrites += cached->second.rewrites;
        continue;
      }
      ++cache.misses;

      const auto assigned = assignments.find(visit.term);
      if (assigned != assignments.end()) {
        ++rewrites;
        cache.entries.emplace(
            visit.term, BooleanUnitSimplificationCache::Entry{terms.mk_bool(assigned->second), 1});
        continue;
      }

      const core::Term& term = terms.get(visit.term);
      const core::TermKind kind = term.kind;
      const bool rewrites_children = kind == core::TermKind::Equality ||
                                     kind == core::TermKind::Not || kind == core::TermKind::And ||
                                     kind == core::TermKind::Or || kind == core::TermKind::Implies;
      if (!rewrites_children) {
        cache.entries.emplace(visit.term, BooleanUnitSimplificationCache::Entry{visit.term, 0});
        continue;
      }

      pending.push_back(Visit{visit.term, true, rewrites});
      for (auto child = term.children.rbegin(); child != term.children.rend(); ++child) {
        pending.push_back(Visit{*child, false, 0});
      }
      continue;
    }

    const core::Term& term = terms.get(visit.term);
    const core::TermKind kind = term.kind;
    const std::vector<core::TermId> children = term.children;
    std::vector<core::TermId> rewritten_children;
    rewritten_children.reserve(children.size());
    for (const core::TermId child : children) {
      rewritten_children.push_back(cache.entries.at(child).term);
    }

    core::TermId simplified = visit.term;
    switch (kind) {
      case core::TermKind::BoolValue:
      case core::TermKind::Constant:
      case core::TermKind::Application:
      case core::TermKind::Rational:
      case core::TermKind::Plus:
      case core::TermKind::Times:
      case core::TermKind::Le:
      case core::TermKind::Lt:
        assert(false && "leaf term reached Boolean-unit post-order reconstruction");
        break;
      case core::TermKind::Equality:
        if (rewritten_children.at(0) != children.at(0) ||
            rewritten_children.at(1) != children.at(1)) {
          simplified = terms.mk_eq(rewritten_children.at(0), rewritten_children.at(1));
        }
        break;
      case core::TermKind::Not:
        simplified = terms.mk_not(rewritten_children.at(0));
        break;
      case core::TermKind::And:
        simplified = terms.mk_and(std::move(rewritten_children));
        break;
      case core::TermKind::Or:
        simplified = terms.mk_or(std::move(rewritten_children));
        break;
      case core::TermKind::Implies:
        simplified = terms.mk_implies(rewritten_children.at(0), rewritten_children.at(1));
        break;
    }
    cache.entries.emplace(visit.term, BooleanUnitSimplificationCache::Entry{
                                          simplified, rewrites - visit.rewrites_before});
  }
  return cache.entries.at(term_id).term;
}

std::pair<core::TermId, core::TermId> representative_pair(
    theory::equality::IncrementalCongruenceClosure& closure, core::TermId lhs, core::TermId rhs) {
  closure.add_term(lhs);
  closure.add_term(rhs);
  closure.saturate();
  return ordered_pair(closure.class_representative(lhs), closure.class_representative(rhs));
}

KnownDisequalityClasses index_known_disequalities(
    theory::equality::IncrementalCongruenceClosure& closure,
    const std::vector<std::pair<core::TermId, core::TermId>>& disequalities) {
  KnownDisequalityClasses result;
  result.pairs.reserve(disequalities.size());
  for (const auto& [lhs, rhs] : disequalities) {
    result.pairs.insert(representative_pair(closure, lhs, rhs));
  }
  return result;
}

bool is_known_disequal(theory::equality::IncrementalCongruenceClosure& closure,
                       const KnownDisequalityClasses& disequalities, core::TermId lhs,
                       core::TermId rhs) {
  return disequalities.pairs.contains(representative_pair(closure, lhs, rhs));
}

core::TermId simplify_with_forced_atoms(core::TermManager& terms, core::TermId term_id,
                                        const std::unordered_map<core::TermId, bool>& assignments,
                                        theory::equality::IncrementalCongruenceClosure& closure,
                                        const KnownDisequalityClasses& disequalities,
                                        std::unordered_map<core::TermId, core::TermId>& memo,
                                        std::size_t& rewrites) {
  struct Visit {
    core::TermId term;
    bool expanded;
  };

  std::vector<Visit> pending{{term_id, false}};
  while (!pending.empty()) {
    const Visit visit = pending.back();
    pending.pop_back();
    if (memo.contains(visit.term)) {
      continue;
    }

    const auto assigned = assignments.find(visit.term);
    if (assigned != assignments.end()) {
      ++rewrites;
      memo.emplace(visit.term, terms.mk_bool(assigned->second));
      continue;
    }

    const core::Term& term = terms.get(visit.term);
    const core::TermKind kind = term.kind;
    const bool rewrites_children = kind == core::TermKind::Equality ||
                                   kind == core::TermKind::Not || kind == core::TermKind::And ||
                                   kind == core::TermKind::Or || kind == core::TermKind::Implies;
    if (!rewrites_children) {
      memo.emplace(visit.term, visit.term);
      continue;
    }
    if (!visit.expanded) {
      pending.push_back(Visit{visit.term, true});
      for (auto child = term.children.rbegin(); child != term.children.rend(); ++child) {
        if (!memo.contains(*child)) {
          pending.push_back(Visit{*child, false});
        }
      }
      continue;
    }

    // Copy before calling TermManager constructors, which may grow term storage.
    const std::vector<core::TermId> children = term.children;
    std::vector<core::TermId> rewritten_children;
    rewritten_children.reserve(children.size());
    for (const core::TermId child : children) {
      rewritten_children.push_back(memo.at(child));
    }

    core::TermId rewritten = visit.term;
    switch (kind) {
      case core::TermKind::BoolValue:
      case core::TermKind::Constant:
      case core::TermKind::Application:
      case core::TermKind::Rational:
      case core::TermKind::Plus:
      case core::TermKind::Times:
      case core::TermKind::Le:
      case core::TermKind::Lt:
        assert(false && "leaf term reached forced-atom post-order reconstruction");
        break;
      case core::TermKind::Equality: {
        const core::TermId lhs = rewritten_children.at(0);
        const core::TermId rhs = rewritten_children.at(1);
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
        rewritten = terms.mk_not(rewritten_children.at(0));
        break;
      case core::TermKind::And:
        rewritten = terms.mk_and(std::move(rewritten_children));
        break;
      case core::TermKind::Or:
        rewritten = terms.mk_or(std::move(rewritten_children));
        break;
      case core::TermKind::Implies:
        rewritten = terms.mk_implies(rewritten_children.at(0), rewritten_children.at(1));
        break;
    }
    if (rewritten != visit.term) {
      ++rewrites;
    }
    memo.emplace(visit.term, rewritten);
  }
  return memo.at(term_id);
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
                             theory::equality::IncrementalCongruenceClosure& closure) {
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
                                   theory::equality::IncrementalCongruenceClosure& closure,
                                   std::vector<bool>& visited, std::size_t& nodes,
                                   std::size_t& memo_hits) {
  std::vector<core::TermId> work{assertion};
  while (!work.empty()) {
    const core::TermId term = work.back();
    work.pop_back();
    if (visited.at(term)) {
      ++memo_hits;
      continue;
    }
    visited[term] = true;
    ++nodes;
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

struct UnitEqualityPropagationShape {
  std::size_t unit_equalities = 0;
  std::size_t equality_nodes = 0;
};

std::size_t count_top_level_unit_equalities(const core::TermManager& terms,
                                            core::TermId assertion) {
  const core::Term& node = terms.get(assertion);
  if (node.kind == core::TermKind::Equality) {
    return 1;
  }
  if (node.kind != core::TermKind::And) {
    return 0;
  }
  std::size_t count = 0;
  for (const core::TermId child : node.children) {
    count += count_top_level_unit_equalities(terms, child);
  }
  return count;
}

std::size_t count_equality_nodes(const core::TermManager& terms, core::TermId assertion,
                                 std::unordered_map<core::TermId, std::size_t>& memo) {
  struct Visit {
    core::TermId term;
    bool expanded;
  };

  std::vector<Visit> pending{{assertion, false}};
  while (!pending.empty()) {
    const Visit visit = pending.back();
    pending.pop_back();
    if (memo.contains(visit.term)) {
      continue;
    }

    const core::Term& node = terms.get(visit.term);
    if (node.kind == core::TermKind::Equality) {
      memo.emplace(visit.term, 1);
      continue;
    }
    if (!visit.expanded) {
      pending.push_back(Visit{visit.term, true});
      for (auto child = node.children.rbegin(); child != node.children.rend(); ++child) {
        if (!memo.contains(*child)) {
          pending.push_back(Visit{*child, false});
        }
      }
      continue;
    }

    std::size_t count = 0;
    for (const core::TermId child : node.children) {
      count += memo.at(child);
    }
    memo.emplace(visit.term, count);
  }
  return memo.at(assertion);
}

UnitEqualityPropagationShape unit_equality_propagation_shape(
    const core::TermManager& terms, const std::vector<core::TermId>& assertions) {
  UnitEqualityPropagationShape shape;
  for (const core::TermId assertion : assertions) {
    shape.unit_equalities += count_top_level_unit_equalities(terms, assertion);
  }
  if (shape.unit_equalities == 0) {
    return shape;
  }
  std::unordered_map<core::TermId, std::size_t> equality_count_memo;
  for (const core::TermId assertion : assertions) {
    shape.equality_nodes += count_equality_nodes(terms, assertion, equality_count_memo);
  }
  return shape;
}

core::TermId simplify_with_congruence(core::TermManager& terms, core::TermId term,
                                      theory::equality::IncrementalCongruenceClosure& closure,
                                      std::unordered_map<core::TermId, core::TermId>& memo) {
  struct Visit {
    core::TermId term;
    bool expanded;
  };

  std::vector<Visit> pending{{term, false}};
  while (!pending.empty()) {
    const Visit visit = pending.back();
    pending.pop_back();
    if (memo.contains(visit.term)) {
      continue;
    }

    const core::Term& node = terms.get(visit.term);
    const core::TermKind kind = node.kind;
    const bool rewrites_children = kind == core::TermKind::Not || kind == core::TermKind::And ||
                                   kind == core::TermKind::Or || kind == core::TermKind::Implies;
    if (!rewrites_children) {
      const core::TermId result =
          kind == core::TermKind::Equality &&
                  closure.are_equal(node.children.at(0), node.children.at(1))
              ? terms.mk_bool(true)
              : visit.term;
      memo.emplace(visit.term, result);
      continue;
    }
    if (!visit.expanded) {
      pending.push_back(Visit{visit.term, true});
      for (auto child = node.children.rbegin(); child != node.children.rend(); ++child) {
        if (!memo.contains(*child)) {
          pending.push_back(Visit{*child, false});
        }
      }
      continue;
    }

    // Copy before calling TermManager constructors, which may grow term storage.
    const std::vector<core::TermId> original_children = node.children;
    std::vector<core::TermId> rewritten_children;
    rewritten_children.reserve(original_children.size());
    bool changed = false;
    for (const core::TermId child : original_children) {
      const core::TermId rewritten = memo.at(child);
      changed = changed || rewritten != child;
      rewritten_children.push_back(rewritten);
    }

    core::TermId result = visit.term;
    if (changed) {
      switch (kind) {
        case core::TermKind::Not:
          result = terms.mk_not(rewritten_children.at(0));
          break;
        case core::TermKind::And:
          result = terms.mk_and(std::move(rewritten_children));
          break;
        case core::TermKind::Or:
          result = terms.mk_or(std::move(rewritten_children));
          break;
        case core::TermKind::Implies:
          result = terms.mk_implies(rewritten_children.at(0), rewritten_children.at(1));
          break;
        case core::TermKind::BoolValue:
        case core::TermKind::Constant:
        case core::TermKind::Application:
        case core::TermKind::Equality:
        case core::TermKind::Rational:
        case core::TermKind::Plus:
        case core::TermKind::Times:
        case core::TermKind::Le:
        case core::TermKind::Lt:
          assert(false && "leaf term reached congruence post-order reconstruction");
          break;
      }
    }
    memo.emplace(visit.term, result);
  }
  return memo.at(term);
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

AdditionalFiniteDomainFacts build_additional_finite_domain_facts(
    core::TermManager& terms,
    const std::vector<std::pair<core::TermId, core::TermId>>& additional_equalities,
    const std::vector<std::pair<core::TermId, core::TermId>>& additional_disequalities) {
  AdditionalFiniteDomainFacts facts(terms);
  for (const auto& [lhs, rhs] : additional_equalities) {
    facts.closure.assert_equal(lhs, rhs);
  }
  for (const auto& [lhs, rhs] : additional_disequalities) {
    facts.closure.add_term(lhs);
    facts.closure.add_term(rhs);
  }
  facts.closure.saturate();
  facts.disequality_representatives.reserve(additional_disequalities.size());
  for (const auto& [lhs, rhs] : additional_disequalities) {
    facts.disequality_representatives.insert(representative_pair(facts.closure, lhs, rhs));
  }
  return facts;
}

bool is_finite_domain_disequality(
    core::TermId lhs, core::TermId rhs,
    const std::unordered_set<std::pair<core::TermId, core::TermId>, PairHash>& disequalities,
    AdditionalFiniteDomainFacts& additional_facts) {
  if (disequalities.contains(ordered_pair(lhs, rhs))) {
    return true;
  }
  return additional_facts.disequality_representatives.contains(
      representative_pair(additional_facts.closure, lhs, rhs));
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

std::unordered_set<core::TermId> finite_domain_disequality_endpoints(
    const core::TermManager& terms,
    const std::unordered_set<std::pair<core::TermId, core::TermId>, PairHash>& disequalities) {
  std::unordered_set<core::TermId> values;
  for (const auto& [lhs, rhs] : disequalities) {
    const core::Term& lhs_term = terms.get(lhs);
    const core::Term& rhs_term = terms.get(rhs);
    if (lhs_term.sort != terms.symbols().bool_sort()) {
      values.insert(lhs);
    }
    if (rhs_term.sort != terms.symbols().bool_sort()) {
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
  const core::SymbolId node_symbol = node.symbol;
  const util::Rational node_rational = node.rational_value;
  core::TermId rewritten = term;
  switch (node.kind) {
    case core::TermKind::BoolValue:
    case core::TermKind::Constant:
    case core::TermKind::Rational:
      break;
    case core::TermKind::Plus: {
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
        rewritten = terms.mk_plus(std::move(children));
      }
      break;
    }
    case core::TermKind::Times: {
      const core::TermId rewritten_child = rewrite_with_ground_unit_equalities(
          terms, node_children.at(0), replacements, memo, rewrites);
      if (rewritten_child != node_children.at(0)) {
        rewritten = terms.mk_times(node_rational, rewritten_child);
      }
      break;
    }
    case core::TermKind::Le:
      rewritten = terms.mk_le(rewrite_with_ground_unit_equalities(terms, node_children.at(0),
                                                                  replacements, memo, rewrites),
                              rewrite_with_ground_unit_equalities(terms, node_children.at(1),
                                                                  replacements, memo, rewrites));
      break;
    case core::TermKind::Lt:
      rewritten = terms.mk_lt(rewrite_with_ground_unit_equalities(terms, node_children.at(0),
                                                                  replacements, memo, rewrites),
                              rewrite_with_ground_unit_equalities(terms, node_children.at(1),
                                                                  replacements, memo, rewrites));
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
        rewritten = terms.mk_app(node_symbol, std::move(children));
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
  struct Visit {
    core::TermId term;
    bool expanded;
  };

  std::vector<Visit> pending{{term_id, false}};
  while (!pending.empty()) {
    const Visit visit = pending.back();
    pending.pop_back();
    if (memo.contains(visit.term)) {
      continue;
    }

    const auto replacement = replacements.find(visit.term);
    const core::Term& term = terms.get(visit.term);
    if (!visit.expanded) {
      pending.push_back(Visit{visit.term, true});
      if (replacement != replacements.end()) {
        if (!memo.contains(replacement->second)) {
          pending.push_back(Visit{replacement->second, false});
        }
      } else {
        for (auto child = term.children.rbegin(); child != term.children.rend(); ++child) {
          if (!memo.contains(*child)) {
            pending.push_back(Visit{*child, false});
          }
        }
      }
      continue;
    }

    core::TermId rewritten = visit.term;
    if (replacement != replacements.end()) {
      rewritten = memo.at(replacement->second);
    } else {
      // Copy before calling TermManager constructors, which may grow term storage.
      const core::TermKind kind = term.kind;
      const std::vector<core::TermId> children = term.children;
      const core::SymbolId symbol = term.symbol;
      const util::Rational rational_value = term.rational_value;
      std::vector<core::TermId> rewritten_children;
      rewritten_children.reserve(children.size());
      bool changed = false;
      for (const core::TermId child : children) {
        const core::TermId rewritten_child = memo.at(child);
        changed = changed || rewritten_child != child;
        rewritten_children.push_back(rewritten_child);
      }

      switch (kind) {
        case core::TermKind::BoolValue:
        case core::TermKind::Constant:
        case core::TermKind::Rational:
          break;
        case core::TermKind::Plus:
          if (changed) {
            rewritten = terms.mk_plus(std::move(rewritten_children));
          }
          break;
        case core::TermKind::Times:
          if (changed) {
            rewritten = terms.mk_times(rational_value, rewritten_children.at(0));
          }
          break;
        case core::TermKind::Le:
          rewritten = terms.mk_le(rewritten_children.at(0), rewritten_children.at(1));
          break;
        case core::TermKind::Lt:
          rewritten = terms.mk_lt(rewritten_children.at(0), rewritten_children.at(1));
          break;
        case core::TermKind::Application:
          if (changed) {
            rewritten = terms.mk_app(symbol, std::move(rewritten_children));
          }
          break;
        case core::TermKind::Equality:
          rewritten = terms.mk_eq(rewritten_children.at(0), rewritten_children.at(1));
          break;
        case core::TermKind::Not:
          rewritten = terms.mk_not(rewritten_children.at(0));
          break;
        case core::TermKind::And:
          rewritten = terms.mk_and(std::move(rewritten_children));
          break;
        case core::TermKind::Or:
          rewritten = terms.mk_or(std::move(rewritten_children));
          break;
        case core::TermKind::Implies:
          rewritten = terms.mk_implies(rewritten_children.at(0), rewritten_children.at(1));
          break;
      }
    }
    if (rewritten != visit.term) {
      ++rewrites;
    }
    memo.emplace(visit.term, rewritten);
  }
  return memo.at(term_id);
}

bool is_real_equality(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  return term.kind == core::TermKind::Equality &&
         terms.get(term.children.at(0)).sort == terms.symbols().real_sort();
}

core::TermId lower_real_equalities_rewrite(core::TermManager& terms, core::TermId term_id,
                                           std::unordered_map<core::TermId, core::TermId>& memo,
                                           RealEqualityLoweringResult& result,
                                           bool enable_guarded_real_equality_distribution) {
  struct Visit {
    core::TermId term;
    bool expanded;
  };

  const auto distributed_equality_child =
      [&terms, enable_guarded_real_equality_distribution](
          const std::vector<core::TermId>& children) -> std::optional<core::TermId> {
    std::optional<core::TermId> equality_child;
    if (enable_guarded_real_equality_distribution) {
      for (const core::TermId child : children) {
        if (!is_real_equality(terms, child)) {
          continue;
        }
        if (equality_child.has_value()) {
          equality_child.reset();
          break;
        }
        equality_child = child;
      }
    }
    return equality_child;
  };

  std::vector<Visit> pending{{term_id, false}};
  while (!pending.empty()) {
    const Visit visit = pending.back();
    pending.pop_back();
    if (memo.contains(visit.term)) {
      continue;
    }

    const core::Term& term = terms.get(visit.term);
    const core::TermKind kind = term.kind;
    const bool rewrites_children = kind == core::TermKind::Application ||
                                   kind == core::TermKind::Equality ||
                                   kind == core::TermKind::Not || kind == core::TermKind::And ||
                                   kind == core::TermKind::Or || kind == core::TermKind::Implies;
    if (!rewrites_children) {
      memo.emplace(visit.term, visit.term);
      continue;
    }
    if (!visit.expanded) {
      pending.push_back(Visit{visit.term, true});
      const std::optional<core::TermId> equality_child =
          kind == core::TermKind::Or && term.children.size() > 1
              ? distributed_equality_child(term.children)
              : std::nullopt;
      if (equality_child.has_value()) {
        const core::Term& equality = terms.get(*equality_child);
        for (auto child = equality.children.rbegin(); child != equality.children.rend(); ++child) {
          if (!memo.contains(*child)) {
            pending.push_back(Visit{*child, false});
          }
        }
        for (auto child = term.children.rbegin(); child != term.children.rend(); ++child) {
          if (*child != *equality_child && !memo.contains(*child)) {
            pending.push_back(Visit{*child, false});
          }
        }
      } else {
        for (auto child = term.children.rbegin(); child != term.children.rend(); ++child) {
          if (!memo.contains(*child)) {
            pending.push_back(Visit{*child, false});
          }
        }
      }
      continue;
    }

    // Copy before calling TermManager constructors, which may grow term storage.
    const std::vector<core::TermId> children = term.children;
    const core::SymbolId symbol = term.symbol;
    core::TermId rewritten = visit.term;
    switch (kind) {
      case core::TermKind::BoolValue:
      case core::TermKind::Constant:
      case core::TermKind::Rational:
      case core::TermKind::Plus:
      case core::TermKind::Times:
      case core::TermKind::Le:
      case core::TermKind::Lt:
        assert(false && "leaf term reached real-equality post-order reconstruction");
        break;
      case core::TermKind::Application: {
        std::vector<core::TermId> rewritten_children;
        rewritten_children.reserve(children.size());
        bool changed = false;
        for (const core::TermId child : children) {
          const core::TermId rewritten_child = memo.at(child);
          changed = changed || rewritten_child != child;
          rewritten_children.push_back(rewritten_child);
        }
        if (changed) {
          rewritten = terms.mk_app(symbol, std::move(rewritten_children));
        }
        break;
      }
      case core::TermKind::Equality: {
        const core::TermId lhs = memo.at(children.at(0));
        const core::TermId rhs = memo.at(children.at(1));
        if (terms.get(lhs).sort == terms.symbols().real_sort()) {
          rewritten = terms.mk_and({terms.mk_le(lhs, rhs), terms.mk_le(rhs, lhs)});
          ++result.lowered;
        } else {
          rewritten = terms.mk_eq(lhs, rhs);
        }
        break;
      }
      case core::TermKind::Not:
        rewritten = terms.mk_not(memo.at(children.at(0)));
        break;
      case core::TermKind::And: {
        std::vector<core::TermId> rewritten_children;
        rewritten_children.reserve(children.size());
        for (const core::TermId child : children) {
          rewritten_children.push_back(memo.at(child));
        }
        rewritten = terms.mk_and(std::move(rewritten_children));
        break;
      }
      case core::TermKind::Or: {
        const std::optional<core::TermId> equality_child =
            children.size() > 1 ? distributed_equality_child(children) : std::nullopt;
        if (equality_child.has_value()) {
          const core::Term& equality = terms.get(*equality_child);
          const core::TermId lhs = memo.at(equality.children.at(0));
          const core::TermId rhs = memo.at(equality.children.at(1));
          std::vector<core::TermId> le_children;
          std::vector<core::TermId> ge_children;
          le_children.reserve(children.size());
          ge_children.reserve(children.size());
          for (const core::TermId child : children) {
            if (child == *equality_child) {
              continue;
            }
            const core::TermId rewritten_child = memo.at(child);
            le_children.push_back(rewritten_child);
            ge_children.push_back(rewritten_child);
          }
          le_children.push_back(terms.mk_le(lhs, rhs));
          ge_children.push_back(terms.mk_le(rhs, lhs));
          rewritten = terms.mk_and(
              {terms.mk_or(std::move(le_children)), terms.mk_or(std::move(ge_children))});
          ++result.lowered;
          ++result.guarded_distributed;
          break;
        }
        std::vector<core::TermId> rewritten_children;
        rewritten_children.reserve(children.size());
        for (const core::TermId child : children) {
          rewritten_children.push_back(memo.at(child));
        }
        rewritten = terms.mk_or(std::move(rewritten_children));
        break;
      }
      case core::TermKind::Implies:
        rewritten = terms.mk_implies(memo.at(children.at(0)), memo.at(children.at(1)));
        break;
    }
    memo.emplace(visit.term, rewritten);
  }
  return memo.at(term_id);
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
    case core::TermKind::Rational:
    case core::TermKind::Plus:
    case core::TermKind::Times:
    case core::TermKind::Le:
    case core::TermKind::Lt:
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
                                    const std::vector<SymmetryTerm>& terms_to_restrict,
                                    std::unordered_set<core::TermId>& emitted,
                                    UfSymmetryBreakingResult& result) {
  // Déharbe et al., Algorithm 1, tracks constants already used by selected fixed
  // terms. Moving terms are filtered out before this fallback is reached.
  std::unordered_set<core::TermId> used_constants;
  for (const SymmetryTerm& term : terms_to_restrict) {
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
                                                    UnitEqualityPropagationStats* stats) {
  // This is a deliberately small preprocessing closure, not a SAT/theory
  // shortcut. Unit equalities already asserted at top level, including facts
  // derived by the equality bridge, are fed to the same ground congruence
  // relation used by the EUF solver, then equality atoms decided by that unit
  // relation are simplified before Boolean abstraction.
  const UnitEqualityPropagationShape shape = unit_equality_propagation_shape(terms, assertions);
  if (shape.unit_equalities == 0 || shape.equality_nodes <= shape.unit_equalities) {
    if (stats != nullptr) {
      *stats = UnitEqualityPropagationStats{};
    }
    return assertions;
  }

  theory::equality::IncrementalCongruenceClosure closure(terms);
  for (const core::TermId assertion : assertions) {
    collect_unit_equalities(terms, assertion, closure);
  }
  std::vector<bool> visited(terms.size(), false);
  std::size_t query_nodes = 0;
  std::size_t query_memo_hits = 0;
  for (const core::TermId assertion : assertions) {
    register_equality_query_terms(terms, assertion, closure, visited, query_nodes, query_memo_hits);
  }
  closure.saturate();

  std::vector<core::TermId> simplified;
  simplified.reserve(assertions.size());
  std::unordered_set<core::TermId> emitted;
  std::unordered_map<core::TermId, core::TermId> simplification_memo;
  std::size_t added = 0;
  for (const core::TermId assertion : assertions) {
    if (emitted.insert(assertion).second) {
      simplified.push_back(assertion);
    }
    const core::TermId consequence =
        simplify_with_congruence(terms, assertion, closure, simplification_memo);
    const core::Term& consequence_node = terms.get(consequence);
    const bool true_consequence =
        consequence_node.kind == core::TermKind::BoolValue && consequence_node.bool_value;
    if (consequence != assertion && !true_consequence && emitted.insert(consequence).second) {
      simplified.push_back(consequence);
      ++added;
    }
  }
  if (stats != nullptr) {
    const theory::equality::IncrementalCongruenceClosureStats& closure_stats = closure.stats();
    stats->consequences_added = added;
    stats->query_nodes = query_nodes;
    stats->query_memo_hits = query_memo_hits;
    stats->add_term_requests = closure_stats.add_term_requests;
    stats->add_term_seen_hits = closure_stats.add_term_seen_hits;
  }
  return simplified;
}

RealEqualityLoweringResult lower_real_equalities(core::TermManager& terms,
                                                 const std::vector<core::TermId>& assertions,
                                                 bool enable_guarded_real_equality_distribution) {
  RealEqualityLoweringResult result;
  result.assertions.reserve(assertions.size());
  std::unordered_map<core::TermId, core::TermId> memo;
  for (const core::TermId assertion : assertions) {
    result.assertions.push_back(lower_real_equalities_rewrite(
        terms, assertion, memo, result, enable_guarded_real_equality_distribution));
  }
  return result;
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
  result.substitutions.reserve(replacements.size());
  for (const auto& [term, representative] : replacements) {
    if (term != representative) {
      result.substitutions.emplace_back(term, representative);
    }
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
    core::TermManager& terms, const std::vector<core::TermId>& assertions,
    bool enable_gaussian_elimination) {
  // This is satisfiability-preserving definitional elimination for free
  // uninterpreted constants: a unit equality x = t may replace x when x does
  // not occur in t. It is deliberately smaller than a full solve-eqs tactic.
  SolvedConstantSubstitutionResult result;
  result.assertions = assertions;

  std::vector<SolvedConstantCandidate> candidates;
  for (const core::TermId assertion : assertions) {
    collect_solved_constant_candidates(terms, assertion, candidates);
  }
  if (enable_gaussian_elimination) {
    std::vector<SolvedConstantCandidate> gaussian_candidates =
        collect_gaussian_elimination_candidates(terms, assertions);
    result.gaussian_candidates = gaussian_candidates.size();
    candidates.insert(candidates.end(), gaussian_candidates.begin(), gaussian_candidates.end());
  }

  result.candidates = candidates.size();
  if (candidates.empty()) {
    return result;
  }

  std::unordered_set<core::TermId> candidate_variables;
  for (const SolvedConstantCandidate& candidate : candidates) {
    candidate_variables.insert(candidate.variable);
  }

  std::vector<std::size_t> occurrences(terms.size());
  SubtreeOccurrenceMap subtree_occurrence_memo;
  for (const core::TermId assertion : assertions) {
    count_term_occurrences(terms, assertion, occurrences, candidate_variables,
                           subtree_occurrence_memo);
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
  result.substitutions.reserve(replacements.size());
  for (const auto& [variable, definition] : replacements) {
    result.substitutions.emplace_back(variable, definition);
  }
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
  if (assignments.values.empty()) {
    result.assertions = assertions;
    return result;
  }
  result.assignments.reserve(assignments.values.size());
  for (const auto& [term, value] : assignments.values) {
    result.assignments.emplace_back(term, value);
  }
  result.assertions.reserve(assertions.size());
  std::unordered_set<core::TermId> emitted;
  for (const auto& [term, value] : assignments.values) {
    const core::TermId unit = value ? term : terms.mk_not(term);
    if (emitted.insert(unit).second) {
      result.assertions.push_back(unit);
    }
  }
  BooleanUnitSimplificationCache cache;
  for (const core::TermId assertion : assertions) {
    const core::TermId simplified =
        simplify_with_boolean_units(terms, assertion, assignments.values, cache, result.rewrites);
    const core::Term& node = terms.get(simplified);
    if (node.kind == core::TermKind::BoolValue && node.bool_value) {
      continue;
    }
    if (emitted.insert(simplified).second) {
      result.assertions.push_back(simplified);
    }
  }
  result.memo_hits = cache.hits;
  result.memo_misses = cache.misses;
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
  result.assignments = assignments.units;
  result.equality_pairs = assignments.equalities;
  result.disequality_pairs = assignments.disequalities;
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
    BooleanUnitSimplificationCache cache;
    for (const core::TermId assertion : assertions) {
      const core::TermId simplified =
          simplify_with_boolean_units(terms, assertion, assignments.values, cache, result.rewrites);
      const core::Term& node = terms.get(simplified);
      if (node.kind == core::TermKind::BoolValue && node.bool_value) {
        continue;
      }
      if (emitted.insert(simplified).second) {
        result.assertions.push_back(simplified);
      }
    }
    result.memo_hits = cache.hits;
    result.memo_misses = cache.misses;
    return result;
  }

  theory::equality::IncrementalCongruenceClosure closure(terms);
  for (const auto& [lhs, rhs] : assignments.equalities) {
    closure.assert_equal(lhs, rhs);
  }
  for (const auto& [lhs, rhs] : assignments.disequalities) {
    closure.add_term(lhs);
    closure.add_term(rhs);
  }
  std::vector<bool> visited(terms.size(), false);
  for (const core::TermId assertion : assertions) {
    register_equality_query_terms(terms, assertion, closure, visited, result.query_nodes,
                                  result.query_memo_hits);
  }
  closure.saturate();
  const KnownDisequalityClasses known_disequalities =
      index_known_disequalities(closure, assignments.disequalities);

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
    const core::TermId simplified = simplify_with_forced_atoms(
        terms, assertion, assignments.values, closure, known_disequalities, memo, result.rewrites);
    const core::Term& node = terms.get(simplified);
    if (node.kind == core::TermKind::BoolValue && node.bool_value) {
      continue;
    }
    if (emitted.insert(simplified).second) {
      result.assertions.push_back(simplified);
    }
  }
  const theory::equality::IncrementalCongruenceClosureStats& closure_stats = closure.stats();
  result.add_term_requests = closure_stats.add_term_requests;
  result.add_term_seen_hits = closure_stats.add_term_seen_hits;
  return result;
}

FiniteDomainAtMostOne::FiniteDomainAtMostOne(core::TermManager& terms) : terms_(terms) {}

FiniteDomainAtMostOneResult FiniteDomainAtMostOne::generate(
    const std::vector<core::TermId>& assertions) {
  return generate(assertions, {}, {});
}

FiniteDomainAtMostOneResult FiniteDomainAtMostOne::generate(
    const std::vector<core::TermId>& assertions,
    const std::vector<std::pair<core::TermId, core::TermId>>& additional_disequalities) {
  return generate(assertions, {}, additional_disequalities);
}

FiniteDomainAtMostOneResult FiniteDomainAtMostOne::generate(
    const std::vector<core::TermId>& assertions,
    const std::vector<std::pair<core::TermId, core::TermId>>& additional_equalities,
    const std::vector<std::pair<core::TermId, core::TermId>>& additional_disequalities) {
  // Bradley and Manna's standard EUF congruence setting justifies these as
  // theory lemmas: if asserted finite-domain values are pairwise disequal, one
  // term cannot equal two different values. The pass only emits SAT-visible
  // AMO clauses for domains whose value disequalities are known from explicit
  // top-level facts or from forced-atom facts preserved by the caller.
  FiniteDomainAtMostOneResult result;
  auto disequalities = collect_disequalities(terms_, assertions);
  for (const auto& [lhs, rhs] : additional_disequalities) {
    disequalities.insert(ordered_pair(lhs, rhs));
  }
  AdditionalFiniteDomainFacts additional_facts =
      build_additional_finite_domain_facts(terms_, additional_equalities, additional_disequalities);
  const auto choices = collect_finite_domain_choices(terms_, assertions, disequalities);
  result.choices = choices.size();
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
  auto emit_distinct_endpoint_clauses = [&](core::TermId lhs, core::TermId rhs) {
    const auto lhs_choice = choices.find(lhs);
    const auto rhs_choice = choices.find(rhs);
    if (lhs_choice == choices.end() || rhs_choice == choices.end() ||
        lhs_choice->second.values != rhs_choice->second.values) {
      return;
    }
    const std::vector<core::TermId> clauses =
        finite_domain_distinct_endpoint_clauses(terms_, lhs, rhs, lhs_choice->second.values);
    for (const core::TermId clause : clauses) {
      if (emitted.insert(clause).second) {
        result.clauses.push_back(clause);
      }
    }
  };
  for (const auto& [lhs, rhs] : disequalities) {
    emit_distinct_endpoint_clauses(lhs, rhs);
  }
  if (!additional_equalities.empty() && !additional_disequalities.empty()) {
    for (auto lhs_choice = choices.begin(); lhs_choice != choices.end(); ++lhs_choice) {
      for (auto rhs_choice = std::next(lhs_choice); rhs_choice != choices.end(); ++rhs_choice) {
        if (lhs_choice->second.values == rhs_choice->second.values &&
            is_finite_domain_disequality(lhs_choice->first, rhs_choice->first, disequalities,
                                         additional_facts)) {
          emit_distinct_endpoint_clauses(lhs_choice->first, rhs_choice->first);
        }
      }
    }
  }
  const std::unordered_set<core::TermId> value_constants =
      finite_domain_disequality_endpoints(terms_, disequalities);
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

    std::vector<SymmetryTerm> fixed_terms = fixed_symmetry_terms(group);
    if (fixed_terms.empty()) {
      ++result.rejected_noninvariant;
      continue;
    }

    ++result.accepted_sets;
    result.values += group.values.size();
    if (options.value_precedence) {
      const std::size_t constraints_before = result.constraints.size();
      result.terms += fixed_terms.size();
      add_value_precedence_constraints(terms_, fixed_terms, group.values, emitted, result);
      result.value_precedence_terms += fixed_terms.size();
      result.value_precedence_constraints += result.constraints.size() - constraints_before;
      continue;
    }

    result.terms += fixed_terms.size();
    add_paper_symmetry_constraints(terms_, group, fixed_terms, emitted, result);
  }

  return result;
}

namespace {

// Which way a variable is pushed. `Hi` means every constraint bounds it
// from above, so making it small satisfies them all.
enum class MonotoneSide : unsigned char { Hi = 1, Lo = 2, Both = 3 };

struct MonotoneState {
  // Side bits accumulated per candidate variable; `Both` disqualifies.
  std::unordered_map<core::TermId, unsigned char> sides;
  // Polarity each atom is seen at: +1, -1, or 0 once it is seen both ways.
  std::unordered_map<core::TermId, int> atom_polarity;
  // Atoms mentioning each candidate variable.
  std::unordered_map<core::TermId, std::vector<core::TermId>> atoms_of;
};

bool is_real_constant(const core::TermManager& terms, core::TermId id) {
  const core::Term& term = terms.get(id);
  return term.kind == core::TermKind::Constant && term.sort == terms.symbols().real_sort();
}

void mark_side(MonotoneState& state, core::TermId variable, MonotoneSide side) {
  state.sides[variable] |= static_cast<unsigned char>(side);
}

// Disqualifies every Real constant beneath `root`: reaching one outside a
// recognized linear atom means we cannot reason about which way it is
// pushed there (uninterpreted-function arguments are the motivating case).
void disqualify_reals_under(const core::TermManager& terms, core::TermId root,
                            MonotoneState& state) {
  std::vector<core::TermId> stack{root};
  std::unordered_set<core::TermId> seen;
  while (!stack.empty()) {
    const core::TermId id = stack.back();
    stack.pop_back();
    if (!seen.insert(id).second) {
      continue;
    }
    if (is_real_constant(terms, id)) {
      mark_side(state, id, MonotoneSide::Both);
    }
    for (const core::TermId child : terms.get(id).children) {
      stack.push_back(child);
    }
  }
}

// Records the direction each variable of one Le/Lt atom is pushed, given
// the polarity the atom occurs at. `polarity` is +1 or -1; the caller
// handles the ambiguous case.
void record_atom_sides(const core::TermManager& terms, core::TermId atom_id, int polarity,
                       MonotoneState& state) {
  theory::arithmetic::LraAtom atom;
  try {
    atom = theory::arithmetic::normalize_atom(terms, atom_id);
  } catch (const std::invalid_argument&) {
    disqualify_reals_under(terms, atom_id, state);
    return;
  }
  if (atom.relation == theory::arithmetic::LraRelation::Eq) {
    // An equality pins both directions at once.
    disqualify_reals_under(terms, atom_id, state);
    return;
  }
  // An atom with a leaf we cannot evaluate at model-construction time
  // must never be folded, or the discharged constraint could end up
  // unsatisfied in the printed model. Disqualify the whole atom, not just
  // that leaf.
  for (const auto& [leaf, coefficient] : atom.polynomial.terms()) {
    if (!coefficient.is_zero() && !is_real_constant(terms, leaf)) {
      disqualify_reals_under(terms, atom_id, state);
      return;
    }
  }
  for (const auto& [leaf, coefficient] : atom.polynomial.terms()) {
    if (coefficient.is_zero()) {
      continue;
    }
    // `poly <= rhs` at positive polarity bounds a positive-coefficient
    // variable from above; negative polarity asserts `poly > rhs` and
    // flips every direction.
    const bool positive_coefficient = util::Rational(0) < coefficient;
    const bool bounds_above = (polarity > 0) == positive_coefficient;
    mark_side(state, leaf, bounds_above ? MonotoneSide::Hi : MonotoneSide::Lo);
    state.atoms_of[leaf].push_back(atom_id);
  }
}

// Single polarity-aware pass over the assertion DAG. Memoized on
// (term, polarity) so a hash-consed node reached many times is walked
// once per polarity rather than once per path.
void collect_monotone_sides(const core::TermManager& terms,
                            const std::vector<core::TermId>& assertions, MonotoneState& state) {
  std::vector<std::pair<core::TermId, int>> stack;
  std::unordered_set<std::uint64_t> seen;
  const auto key = [](core::TermId id, int polarity) {
    return (static_cast<std::uint64_t>(id) << 2) | static_cast<std::uint64_t>(polarity + 1);
  };
  for (const core::TermId assertion : assertions) {
    stack.emplace_back(assertion, 1);
  }
  while (!stack.empty()) {
    const auto [id, polarity] = stack.back();
    stack.pop_back();
    if (!seen.insert(key(id, polarity)).second) {
      continue;
    }
    const core::Term& term = terms.get(id);
    switch (term.kind) {
      case core::TermKind::Not:
        stack.emplace_back(term.children.at(0), -polarity);
        break;
      case core::TermKind::And:
      case core::TermKind::Or:
        for (const core::TermId child : term.children) {
          stack.emplace_back(child, polarity);
        }
        break;
      case core::TermKind::Implies:
        for (std::size_t i = 0; i + 1 < term.children.size(); ++i) {
          stack.emplace_back(term.children.at(i), -polarity);
        }
        stack.emplace_back(term.children.back(), polarity);
        break;
      case core::TermKind::Le:
      case core::TermKind::Lt: {
        auto [it, inserted] = state.atom_polarity.emplace(id, polarity);
        if (!inserted && it->second != polarity) {
          it->second = 0;
        }
        if (polarity == 0) {
          disqualify_reals_under(terms, id, state);
        } else {
          record_atom_sides(terms, id, polarity, state);
        }
        break;
      }
      case core::TermKind::Equality:
        // Real equalities pin both directions; Boolean equivalences give
        // their operands no definite polarity. Both are handled by
        // refusing to reason about anything beneath.
        disqualify_reals_under(terms, id, state);
        break;
      default:
        // Applications, bare constants, and arithmetic terms reached
        // outside an atom: conservatively opaque.
        disqualify_reals_under(terms, id, state);
        break;
    }
  }
}

}  // namespace

MonotoneEliminationResult eliminate_monotone_variables(
    core::TermManager& terms, const std::vector<core::TermId>& assertions) {
  MonotoneEliminationResult result;
  result.assertions = assertions;

  MonotoneState state;
  collect_monotone_sides(terms, assertions, state);

  std::vector<core::TermId> candidates;
  for (const auto& [variable, side] : state.sides) {
    if (side != static_cast<unsigned char>(MonotoneSide::Hi) &&
        side != static_cast<unsigned char>(MonotoneSide::Lo)) {
      continue;
    }
    if (state.atoms_of.count(variable) == 0) {
      continue;
    }
    candidates.push_back(variable);
  }
  std::sort(candidates.begin(), candidates.end());
  result.candidates = candidates.size();
  if (candidates.empty()) {
    return result;
  }

  // No dependency ordering is needed, which is worth stating because the
  // obvious design (topologically sort by "whose bound mentions whom")
  // both costs more and is wrong: two variables sharing one atom depend on
  // each other, and rejecting that cycle would discard eliminations that
  // are perfectly sound.
  //
  // Any order works. Take any discharged atom and the eliminated variable
  // of it valued *last*: every other leaf is then already valued -- either
  // by the tableau or by an earlier entry -- so that atom yields a real
  // bound on it, and its chosen value is past the tightest of all its
  // bounds. Since every constraint pushes it the same way, going far
  // enough satisfies all of them, that atom included. Earlier variables
  // may skip the atom as unevaluable; the last one still repairs it.
  const std::vector<core::TermId>& ordered = candidates;
  const std::unordered_set<core::TermId> eliminated(ordered.begin(), ordered.end());

  // Every atom mentioning an eliminated variable is discharged. Its
  // polarity is necessarily uniform: an atom seen both ways would have
  // marked its variables `Both` and disqualified them.
  std::unordered_map<core::TermId, bool> folded;
  for (const core::TermId variable : ordered) {
    for (const core::TermId atom_id : state.atoms_of.at(variable)) {
      const int polarity = state.atom_polarity.at(atom_id);
      if (polarity == 0) {
        continue;
      }
      folded.emplace(atom_id, polarity > 0);
    }
  }
  if (folded.empty()) {
    return result;
  }
  result.atoms_folded = folded.size();

  for (const core::TermId variable : ordered) {
    MonotoneEliminatedVariable entry;
    entry.variable = variable;
    entry.bounded_above = state.sides.at(variable) == static_cast<unsigned char>(MonotoneSide::Hi);
    std::vector<core::TermId> atom_ids = state.atoms_of.at(variable);
    std::sort(atom_ids.begin(), atom_ids.end());
    atom_ids.erase(std::unique(atom_ids.begin(), atom_ids.end()), atom_ids.end());
    for (const core::TermId atom_id : atom_ids) {
      const auto found = folded.find(atom_id);
      if (found == folded.end()) {
        continue;
      }
      entry.constraints.emplace_back(atom_id, found->second);
    }
    if (!entry.constraints.empty()) {
      result.eliminated.push_back(std::move(entry));
    }
  }

  // Rebuild the assertions with the discharged atoms replaced by
  // constants. Post-order over the DAG with an explicit stack; each node
  // is rebuilt once.
  std::unordered_map<core::TermId, core::TermId> rewritten;
  const auto rebuild = [&](core::TermId root) {
    std::vector<std::pair<core::TermId, bool>> stack{{root, false}};
    while (!stack.empty()) {
      const auto [id, expanded] = stack.back();
      stack.pop_back();
      if (rewritten.count(id) != 0) {
        continue;
      }
      const auto fold = folded.find(id);
      if (fold != folded.end()) {
        rewritten.emplace(id, terms.mk_bool(fold->second));
        continue;
      }
      const core::Term& term = terms.get(id);
      if (term.children.empty()) {
        rewritten.emplace(id, id);
        continue;
      }
      if (!expanded) {
        stack.emplace_back(id, true);
        for (const core::TermId child : term.children) {
          stack.emplace_back(child, false);
        }
        continue;
      }
      std::vector<core::TermId> children;
      children.reserve(term.children.size());
      bool changed = false;
      for (const core::TermId child : term.children) {
        const core::TermId replacement = rewritten.at(child);
        changed |= replacement != child;
        children.push_back(replacement);
      }
      if (!changed) {
        rewritten.emplace(id, id);
        continue;
      }
      core::TermId replacement = id;
      switch (term.kind) {
        case core::TermKind::Not:
          replacement = terms.mk_not(children.at(0));
          break;
        case core::TermKind::And:
          replacement = terms.mk_and(std::move(children));
          break;
        case core::TermKind::Or:
          replacement = terms.mk_or(std::move(children));
          break;
        case core::TermKind::Implies:
          replacement = terms.mk_implies(children.at(0), children.at(1));
          break;
        case core::TermKind::Equality:
          replacement = terms.mk_eq(children.at(0), children.at(1));
          break;
        default:
          // Arithmetic and application nodes never contain a folded
          // Boolean atom, so they are returned unchanged.
          replacement = id;
          break;
      }
      rewritten.emplace(id, replacement);
    }
    return rewritten.at(root);
  };

  result.assertions.clear();
  result.assertions.reserve(assertions.size());
  for (const core::TermId assertion : assertions) {
    result.assertions.push_back(rebuild(assertion));
  }
  return result;
}

}  // namespace primo::smt
