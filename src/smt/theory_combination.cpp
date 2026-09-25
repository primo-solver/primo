#include "smt/theory_combination.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "sat/lit.hpp"

namespace primo::smt {

namespace {

constexpr std::size_t kHashMix = static_cast<std::size_t>(0x9e3779b97f4a7c15ULL);

struct TermPair {
  core::TermId lhs = core::kInvalidTermId;
  core::TermId rhs = core::kInvalidTermId;

  bool operator==(const TermPair& other) const = default;
};

struct TermPairHash {
  std::size_t operator()(const TermPair& pair) const {
    std::size_t seed = static_cast<std::size_t>(pair.lhs);
    seed ^= static_cast<std::size_t>(pair.rhs) + kHashMix + (seed << 6U) + (seed >> 2U);
    return seed;
  }
};

struct PurificationSiteKey {
  PurificationSiteKind kind = PurificationSiteKind::RealTermInEufAtom;
  core::TermId term = core::kInvalidTermId;
  core::TermId atom = core::kInvalidTermId;

  bool operator==(const PurificationSiteKey& other) const = default;
};

struct PurificationSiteKeyHash {
  std::size_t operator()(const PurificationSiteKey& key) const {
    std::size_t seed = static_cast<std::size_t>(key.term);
    seed ^= static_cast<std::size_t>(key.atom) + kHashMix + (seed << 6U) + (seed >> 2U);
    seed ^= static_cast<std::size_t>(key.kind) + kHashMix + (seed << 6U) + (seed >> 2U);
    return seed;
  }
};

bool is_interface_constant(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.kind != core::TermKind::Constant || term.sort == terms.symbols().bool_sort()) {
    return false;
  }
  return !terms.symbols().get_symbol(term.symbol).internal;
}

TermPair canonical_pair(core::TermId lhs, core::TermId rhs) {
  if (rhs < lhs) {
    std::swap(lhs, rhs);
  }
  return TermPair{lhs, rhs};
}

bool is_lra_atom(const core::TermManager& terms, const core::Term& term) {
  if (term.kind == core::TermKind::Le || term.kind == core::TermKind::Lt) {
    return true;
  }
  return term.kind == core::TermKind::Equality &&
         terms.get(term.children.at(0)).sort == terms.symbols().real_sort();
}

bool is_euf_atom(const core::TermManager& terms, const core::Term& term) {
  if (term.kind == core::TermKind::Equality) {
    return terms.get(term.children.at(0)).sort != terms.symbols().real_sort();
  }
  return (term.kind == core::TermKind::Application || term.kind == core::TermKind::Constant) &&
         term.sort == terms.symbols().bool_sort();
}

void append_purification_site(
    NelsonOppenInterfaceSummary& summary,
    std::unordered_set<PurificationSiteKey, PurificationSiteKeyHash>& seen,
    PurificationSiteKind kind, core::TermId term_id, core::TermId atom_id, core::SortId sort) {
  if (!seen.insert(PurificationSiteKey{kind, term_id, atom_id}).second) {
    return;
  }
  summary.purification_sites.push_back(PurificationSite{term_id, atom_id, sort, kind});
  if (kind == PurificationSiteKind::RealTermInEufAtom) {
    ++summary.euf_real_terms;
  } else {
    ++summary.arithmetic_uninterpreted_applications;
  }
}

// Iterative, visited-node-memoized DAG walks. Recursion here would both risk
// unbounded native-stack depth on deep terms and re-walk shared sub-DAGs once
// per incoming path (the `seen` set below only dedupes *recorded sites*, not
// traversal itself), which is exponential on the diamond-shaped sharing eager
// definition compilation can produce (see docs/architecture.md).
void collect_real_terms_in_euf_atom(
    const core::TermManager& terms, core::TermId root_id, core::TermId atom_id,
    NelsonOppenInterfaceSummary& summary,
    std::unordered_set<PurificationSiteKey, PurificationSiteKeyHash>& seen) {
  std::unordered_set<core::TermId> visited;
  std::vector<core::TermId> pending{root_id};
  while (!pending.empty()) {
    const core::TermId term_id = pending.back();
    pending.pop_back();
    if (!visited.insert(term_id).second) {
      continue;
    }
    const core::Term& term = terms.get(term_id);
    if (term.sort == terms.symbols().real_sort()) {
      append_purification_site(summary, seen, PurificationSiteKind::RealTermInEufAtom, term_id,
                               atom_id, term.sort);
    }
    pending.insert(pending.end(), term.children.begin(), term.children.end());
  }
}

void collect_uninterpreted_applications_in_arithmetic_atom(
    const core::TermManager& terms, core::TermId root_id, core::TermId atom_id,
    NelsonOppenInterfaceSummary& summary,
    std::unordered_set<PurificationSiteKey, PurificationSiteKeyHash>& seen) {
  std::unordered_set<core::TermId> visited;
  std::vector<core::TermId> pending{root_id};
  while (!pending.empty()) {
    const core::TermId term_id = pending.back();
    pending.pop_back();
    if (!visited.insert(term_id).second) {
      continue;
    }
    const core::Term& term = terms.get(term_id);
    if (term.kind == core::TermKind::Application) {
      append_purification_site(summary, seen,
                               PurificationSiteKind::UninterpretedApplicationInArithmeticAtom,
                               term_id, atom_id, term.sort);
    }
    pending.insert(pending.end(), term.children.begin(), term.children.end());
  }
}

}  // namespace

NelsonOppenInterfaceSummary analyze_nelson_oppen_interface(
    const core::TermManager& terms, const std::vector<core::TermId>& assertions) {
  TheoryCombinationManager manager(terms);
  manager.rebuild(assertions);
  return manager.summary();
}

TheoryCombinationManager::TheoryCombinationManager(const core::TermManager& terms)
    : terms_(terms) {}

std::uint64_t TheoryCombinationManager::equality_key(core::TermId lhs, core::TermId rhs) {
  if (rhs < lhs) {
    std::swap(lhs, rhs);
  }
  return (static_cast<std::uint64_t>(lhs) << 32U) | static_cast<std::uint64_t>(rhs);
}

void TheoryCombinationManager::rebuild_equality_index() {
  equality_index_.clear();
  equality_index_.reserve(summary_.equality_registry.size());
  for (std::size_t i = 0; i < summary_.equality_registry.size(); ++i) {
    const InterfaceEqualityRegistryEntry& entry = summary_.equality_registry[i];
    equality_index_.emplace(equality_key(entry.lhs, entry.rhs), i);
  }
}

std::uint32_t TheoryCombinationManager::classification_bit(
    InterfaceEqualityClassification classification) {
  return static_cast<std::uint32_t>(classification);
}

void TheoryCombinationManager::rebuild_from_interface_terms(
    const std::vector<core::TermId>& interface_terms) {
  invalidate_shared_model();
  shared_equality_decisions_ = SharedEqualityDecisionState{};
  summary_ = NelsonOppenInterfaceSummary{};
  interface_sort_by_term_.clear();
  equality_index_.clear();
  value_trail_.clear();
  value_level_starts_.clear();
  std::unordered_map<core::SortId, std::vector<core::TermId>> terms_by_sort;
  for (const core::TermId term_id : interface_terms) {
    const core::Term& term = terms_.get(term_id);
    if (term.sort == terms_.symbols().bool_sort()) {
      continue;
    }
    terms_by_sort[term.sort].push_back(term_id);
  }

  summary_.sort_count = terms_by_sort.size();
  for (auto& [sort, sort_terms] : terms_by_sort) {
    std::sort(sort_terms.begin(), sort_terms.end());
    sort_terms.erase(std::unique(sort_terms.begin(), sort_terms.end()), sort_terms.end());
    summary_.candidate_pairs += sort_terms.size() * (sort_terms.size() - 1U) / 2U;
    for (const core::TermId term_id : sort_terms) {
      summary_.terms.push_back(InterfaceTerm{term_id, sort});
      interface_sort_by_term_.emplace(term_id, sort);
    }
  }
  std::sort(summary_.terms.begin(), summary_.terms.end(),
            [](const InterfaceTerm& lhs, const InterfaceTerm& rhs) {
              return std::pair{lhs.sort, lhs.term} < std::pair{rhs.sort, rhs.term};
            });
  std::sort(
      summary_.equality_registry.begin(), summary_.equality_registry.end(),
      [](const InterfaceEqualityRegistryEntry& lhs, const InterfaceEqualityRegistryEntry& rhs) {
        return std::pair{lhs.lhs, lhs.rhs} < std::pair{rhs.lhs, rhs.rhs};
      });
  rebuild_equality_index();
}

void TheoryCombinationManager::clear_model_internal_classifications() {
  const std::uint32_t model_internal_bit =
      classification_bit(InterfaceEqualityClassification::ModelInternal);
  for (const SharedModelEquality& equality : shared_model_.internal_equalities) {
    const auto found = equality_index_.find(equality_key(equality.lhs, equality.rhs));
    if (found != equality_index_.end()) {
      summary_.equality_registry[found->second].classifications &= ~model_internal_bit;
    }
  }
}

void TheoryCombinationManager::begin_shared_model_round() {
  ++shared_model_.generation;
  shared_model_.valid = true;
  shared_model_.reconciliation_iteration = 0;
  shared_model_.lra_value_classes.clear();
  shared_model_.lra_class_by_term.clear();
  shared_model_.lra_mapped_terms.clear();
  shared_model_.lra_model_spanning_equalities.clear();
  shared_model_.internal_equalities.clear();
  shared_model_.explained_equalities.clear();
  explained_equality_index_.clear();
  shared_model_.lra_separation_atoms.clear();
}

void TheoryCombinationManager::begin_shared_model_iteration(
    const std::vector<std::vector<core::TermId>>& lra_value_classes,
    const std::vector<core::TermId>& lra_mapped_terms) {
  if (!shared_model_.valid) {
    begin_shared_model_round();
  }
  ++shared_model_.reconciliation_iteration;
  clear_model_internal_classifications();
  shared_model_.lra_value_classes.clear();
  shared_model_.lra_class_by_term.clear();
  shared_model_.lra_mapped_terms.clear();
  shared_model_.lra_model_spanning_equalities.clear();
  shared_model_.internal_equalities.clear();
  shared_model_.explained_equalities.clear();
  explained_equality_index_.clear();

  for (const core::TermId term : lra_mapped_terms) {
    if (interface_sort_by_term_.contains(term)) {
      shared_model_.lra_mapped_terms.insert(term);
    }
  }

  shared_model_.lra_value_classes.reserve(lra_value_classes.size());
  for (std::vector<core::TermId> value_class : lra_value_classes) {
    std::erase_if(value_class,
                  [this](core::TermId term) { return !interface_sort_by_term_.contains(term); });
    std::sort(value_class.begin(), value_class.end());
    value_class.erase(std::unique(value_class.begin(), value_class.end()), value_class.end());
    if (value_class.size() < 2) {
      continue;
    }
    for (const core::TermId term : value_class) {
      shared_model_.lra_mapped_terms.insert(term);
    }
    shared_model_.lra_value_classes.push_back(std::move(value_class));
  }
  std::sort(shared_model_.lra_value_classes.begin(), shared_model_.lra_value_classes.end());
  for (std::size_t class_index = 0; class_index < shared_model_.lra_value_classes.size();
       ++class_index) {
    const std::vector<core::TermId>& value_class = shared_model_.lra_value_classes[class_index];
    const core::TermId representative = value_class.front();
    for (const core::TermId term : value_class) {
      shared_model_.lra_class_by_term.emplace(term, class_index);
    }
    for (std::size_t index = 1; index < value_class.size(); ++index) {
      shared_model_.lra_model_spanning_equalities.push_back(
          SharedModelEquality{representative, value_class[index]});
    }
  }
  std::sort(shared_model_.lra_model_spanning_equalities.begin(),
            shared_model_.lra_model_spanning_equalities.end(),
            [](const SharedModelEquality& lhs, const SharedModelEquality& rhs) {
              return std::pair{lhs.lhs, lhs.rhs} < std::pair{rhs.lhs, rhs.rhs};
            });
}

bool TheoryCombinationManager::lra_model_terms_mapped(core::TermId lhs, core::TermId rhs) const {
  return shared_model_.valid && shared_model_.lra_mapped_terms.contains(lhs) &&
         shared_model_.lra_mapped_terms.contains(rhs);
}

bool TheoryCombinationManager::lra_model_agrees_equal(core::TermId lhs, core::TermId rhs) const {
  if (!lra_model_terms_mapped(lhs, rhs)) {
    return false;
  }
  const auto lhs_class = shared_model_.lra_class_by_term.find(lhs);
  const auto rhs_class = shared_model_.lra_class_by_term.find(rhs);
  return lhs_class != shared_model_.lra_class_by_term.end() &&
         rhs_class != shared_model_.lra_class_by_term.end() &&
         lhs_class->second == rhs_class->second;
}

bool TheoryCombinationManager::enqueue_explained_shared_equality(core::TermId lhs, core::TermId rhs,
                                                                 std::vector<sat::Lit> reasons,
                                                                 SharedEqualitySource source) {
  if (!shared_model_.valid || !ensure_interface_pair(lhs, rhs).has_value()) {
    return false;
  }
  if (rhs < lhs) {
    std::swap(lhs, rhs);
  }
  std::sort(reasons.begin(), reasons.end(),
            [](sat::Lit lhs_lit, sat::Lit rhs_lit) { return lhs_lit.value < rhs_lit.value; });
  reasons.erase(std::unique(reasons.begin(), reasons.end(),
                            [](sat::Lit lhs_lit, sat::Lit rhs_lit) {
                              return lhs_lit.value == rhs_lit.value;
                            }),
                reasons.end());
  const std::uint64_t key = equality_key(lhs, rhs);
  const auto existing = explained_equality_index_.find(key);
  if (existing != explained_equality_index_.end()) {
    // Each producer reason proves the same equality. Retaining the shortest one
    // reduces downstream EUF explanation traffic without changing the lemma.
    ExplainedSharedEquality& current = shared_model_.explained_equalities.at(existing->second);
    if (reasons.size() < current.reasons.size()) {
      current.reasons = std::move(reasons);
      current.source = source;
    }
    return false;
  }
  explained_equality_index_.emplace(key, shared_model_.explained_equalities.size());
  shared_model_.explained_equalities.push_back(
      ExplainedSharedEquality{lhs, rhs, std::move(reasons), source});
  return true;
}

std::vector<ExplainedSharedEquality> TheoryCombinationManager::drain_explained_shared_equalities() {
  std::vector<ExplainedSharedEquality> drained = std::move(shared_model_.explained_equalities);
  shared_model_.explained_equalities.clear();
  explained_equality_index_.clear();
  return drained;
}

bool TheoryCombinationManager::record_internal_model_equality(core::TermId lhs, core::TermId rhs) {
  if (!shared_model_.valid || !ensure_interface_pair(lhs, rhs).has_value()) {
    return false;
  }
  if (rhs < lhs) {
    std::swap(lhs, rhs);
  }
  if (has_interface_equality_classification(lhs, rhs,
                                            InterfaceEqualityClassification::ModelInternal)) {
    return false;
  }
  mark_interface_equality_classification(lhs, rhs, InterfaceEqualityClassification::ModelInternal);
  shared_model_.internal_equalities.push_back(SharedModelEquality{lhs, rhs});
  return true;
}

bool TheoryCombinationManager::record_lra_separation_atom(core::TermId atom) {
  if (!shared_model_.valid || atom == core::kInvalidTermId ||
      std::find(shared_model_.lra_separation_atoms.begin(),
                shared_model_.lra_separation_atoms.end(),
                atom) != shared_model_.lra_separation_atoms.end()) {
    return false;
  }
  shared_model_.lra_separation_atoms.push_back(atom);
  return true;
}

void TheoryCombinationManager::invalidate_shared_model() {
  clear_model_internal_classifications();
  shared_model_.valid = false;
  shared_model_.reconciliation_iteration = 0;
  shared_model_.lra_value_classes.clear();
  shared_model_.lra_class_by_term.clear();
  shared_model_.lra_mapped_terms.clear();
  shared_model_.lra_model_spanning_equalities.clear();
  shared_model_.internal_equalities.clear();
  shared_model_.explained_equalities.clear();
  explained_equality_index_.clear();
  shared_model_.lra_separation_atoms.clear();
}

void TheoryCombinationManager::rebuild(const std::vector<core::TermId>& assertions) {
  invalidate_shared_model();
  shared_equality_decisions_ = SharedEqualityDecisionState{};
  summary_ = NelsonOppenInterfaceSummary{};
  interface_sort_by_term_.clear();
  equality_index_.clear();
  value_trail_.clear();
  value_level_starts_.clear();
  std::vector<core::TermId> pending = assertions;
  std::unordered_set<core::TermId> visited;
  std::unordered_map<core::SortId, std::vector<core::TermId>> terms_by_sort;
  std::unordered_map<TermPair, core::TermId, TermPairHash> visible_pairs;
  std::unordered_set<PurificationSiteKey, PurificationSiteKeyHash> purification_sites;

  while (!pending.empty()) {
    const core::TermId term_id = pending.back();
    pending.pop_back();
    if (!visited.insert(term_id).second) {
      continue;
    }

    const core::Term& term = terms_.get(term_id);
    if (is_interface_constant(terms_, term_id)) {
      terms_by_sort[term.sort].push_back(term_id);
    }
    if (term.kind == core::TermKind::Equality && term.children.at(0) != term.children.at(1) &&
        is_interface_constant(terms_, term.children.at(0)) &&
        is_interface_constant(terms_, term.children.at(1))) {
      const core::Term& lhs = terms_.get(term.children.at(0));
      const core::Term& rhs = terms_.get(term.children.at(1));
      if (lhs.sort == rhs.sort) {
        const TermPair pair = canonical_pair(term.children.at(0), term.children.at(1));
        if (visible_pairs.emplace(pair, term_id).second) {
          summary_.visible_equalities.push_back(
              InterfaceEquality{term_id, pair.lhs, pair.rhs, lhs.sort});
        }
      }
    }
    if (is_euf_atom(terms_, term)) {
      collect_real_terms_in_euf_atom(terms_, term_id, term_id, summary_, purification_sites);
    } else if (is_lra_atom(terms_, term)) {
      collect_uninterpreted_applications_in_arithmetic_atom(terms_, term_id, term_id, summary_,
                                                            purification_sites);
    }

    pending.insert(pending.end(), term.children.begin(), term.children.end());
  }

  summary_.sort_count = terms_by_sort.size();
  for (auto& [sort, sort_terms] : terms_by_sort) {
    std::sort(sort_terms.begin(), sort_terms.end());
    sort_terms.erase(std::unique(sort_terms.begin(), sort_terms.end()), sort_terms.end());
    summary_.candidate_pairs += sort_terms.size() * (sort_terms.size() - 1U) / 2U;
    for (const core::TermId term_id : sort_terms) {
      summary_.terms.push_back(InterfaceTerm{term_id, sort});
      interface_sort_by_term_.emplace(term_id, sort);
    }
  }
  for (const auto& [pair, equality] : visible_pairs) {
    const auto sort = interface_sort_by_term_.find(pair.lhs);
    if (sort == interface_sort_by_term_.end()) {
      continue;
    }
    summary_.equality_registry.push_back(InterfaceEqualityRegistryEntry{
        equality, pair.lhs, pair.rhs, sort->second, sat::Lit{0}, InterfaceEqualityStatus::Visible});
  }
  std::sort(summary_.terms.begin(), summary_.terms.end(),
            [](const InterfaceTerm& lhs, const InterfaceTerm& rhs) {
              return std::pair{lhs.sort, lhs.term} < std::pair{rhs.sort, rhs.term};
            });
  std::sort(summary_.visible_equalities.begin(), summary_.visible_equalities.end(),
            [](const InterfaceEquality& lhs, const InterfaceEquality& rhs) {
              return std::pair{lhs.lhs, lhs.rhs} < std::pair{rhs.lhs, rhs.rhs};
            });
  std::sort(
      summary_.equality_registry.begin(), summary_.equality_registry.end(),
      [](const InterfaceEqualityRegistryEntry& lhs, const InterfaceEqualityRegistryEntry& rhs) {
        return std::pair{lhs.lhs, lhs.rhs} < std::pair{rhs.lhs, rhs.rhs};
      });
  std::sort(summary_.purification_sites.begin(), summary_.purification_sites.end(),
            [](const PurificationSite& lhs, const PurificationSite& rhs) {
              return std::tuple{lhs.kind, lhs.atom, lhs.term} <
                     std::tuple{rhs.kind, rhs.atom, rhs.term};
            });
  rebuild_equality_index();
}

std::optional<InterfaceEqualityRegistryEntry> TheoryCombinationManager::interface_equality(
    core::TermId lhs, core::TermId rhs) const {
  const auto found = equality_index_.find(equality_key(lhs, rhs));
  if (found == equality_index_.end()) {
    return std::nullopt;
  }
  return summary_.equality_registry[found->second];
}

std::optional<InterfaceEqualityRegistryEntry> TheoryCombinationManager::ensure_interface_pair(
    core::TermId lhs, core::TermId rhs) {
  if (lhs == rhs) {
    return std::nullopt;
  }
  const TermPair pair = canonical_pair(lhs, rhs);
  if (const auto existing = interface_equality(pair.lhs, pair.rhs); existing.has_value()) {
    return existing;
  }
  const auto lhs_sort = interface_sort_by_term_.find(pair.lhs);
  const auto rhs_sort = interface_sort_by_term_.find(pair.rhs);
  if (lhs_sort == interface_sort_by_term_.end() || rhs_sort == interface_sort_by_term_.end() ||
      lhs_sort->second != rhs_sort->second) {
    return std::nullopt;
  }
  const std::size_t index = summary_.equality_registry.size();
  summary_.equality_registry.push_back(
      InterfaceEqualityRegistryEntry{core::kInvalidTermId, pair.lhs, pair.rhs, lhs_sort->second,
                                     sat::Lit{0}, InterfaceEqualityStatus::Missing});
  equality_index_.emplace(equality_key(pair.lhs, pair.rhs), index);
  ++summary_.missing_equalities;
  return summary_.equality_registry.back();
}

std::optional<InterfaceEqualityRegistryEntry> TheoryCombinationManager::interface_equality(
    core::TermId equality) const {
  const core::Term& term = terms_.get(equality);
  if (term.kind != core::TermKind::Equality || term.children.size() != 2) {
    return std::nullopt;
  }
  return interface_equality(term.children.at(0), term.children.at(1));
}

std::optional<sat::Lit> TheoryCombinationManager::interface_equality_lit(core::TermId lhs,
                                                                         core::TermId rhs) const {
  const auto found = equality_index_.find(equality_key(lhs, rhs));
  if (found == equality_index_.end()) {
    return std::nullopt;
  }
  const sat::Lit lit = summary_.equality_registry[found->second].lit;
  if (lit.value == 0) {
    return std::nullopt;
  }
  return lit;
}

std::optional<sat::Lit> TheoryCombinationManager::interface_equality_lit(
    core::TermId equality) const {
  const core::Term& term = terms_.get(equality);
  if (term.kind != core::TermKind::Equality || term.children.size() != 2) {
    return std::nullopt;
  }
  return interface_equality_lit(term.children.at(0), term.children.at(1));
}

bool TheoryCombinationManager::register_interface_equality_atom(core::TermId equality,
                                                                sat::Lit lit) {
  const core::Term& term = terms_.get(equality);
  if (lit.value == 0 || term.kind != core::TermKind::Equality || term.children.size() != 2) {
    return false;
  }
  const core::TermId lhs = term.children.at(0);
  const core::TermId rhs = term.children.at(1);
  const core::Term& lhs_term = terms_.get(lhs);
  const core::Term& rhs_term = terms_.get(rhs);
  if (lhs_term.sort != rhs_term.sort) {
    return false;
  }

  const auto found = equality_index_.find(equality_key(lhs, rhs));
  if (found == equality_index_.end()) {
    return false;
  }
  InterfaceEqualityRegistryEntry& entry = summary_.equality_registry[found->second];
  if (entry.atom == core::kInvalidTermId) {
    entry.atom = equality;
    if (entry.status == InterfaceEqualityStatus::Missing) {
      --summary_.missing_equalities;
    }
  }
  entry.lit = lit;
  entry.status = InterfaceEqualityStatus::Visible;
  return true;
}

std::size_t TheoryCombinationManager::registered_interface_equality_atom_count() const {
  std::size_t count = 0;
  for (const InterfaceEqualityRegistryEntry& entry : summary_.equality_registry) {
    if (entry.lit.value != 0) {
      ++count;
    }
  }
  return count;
}

InterfaceEqualityValue TheoryCombinationManager::interface_equality_value(core::TermId lhs,
                                                                          core::TermId rhs) const {
  const auto found = equality_index_.find(equality_key(lhs, rhs));
  if (found == equality_index_.end()) {
    return InterfaceEqualityValue::Unknown;
  }
  return summary_.equality_registry[found->second].value;
}

bool TheoryCombinationManager::set_interface_equality_value(core::TermId lhs, core::TermId rhs,
                                                            InterfaceEqualityValue value) {
  const auto found = equality_index_.find(equality_key(lhs, rhs));
  if (found == equality_index_.end()) {
    return false;
  }
  InterfaceEqualityRegistryEntry& entry = summary_.equality_registry[found->second];
  if (entry.value == value) {
    return true;
  }
  value_trail_.push_back(EqualityValueTrailEntry{found->second, entry.value});
  entry.value = value;
  return true;
}

void TheoryCombinationManager::clear_interface_equality_values() {
  for (InterfaceEqualityRegistryEntry& entry : summary_.equality_registry) {
    entry.value = InterfaceEqualityValue::Unknown;
  }
  value_trail_.clear();
  value_level_starts_.clear();
}

InterfaceEqualityAssignment TheoryCombinationManager::interface_equality_assignment(
    core::TermId lhs, core::TermId rhs) const {
  const auto found = equality_index_.find(equality_key(lhs, rhs));
  if (found == equality_index_.end()) {
    return InterfaceEqualityAssignment::Unknown;
  }
  const auto lit = summary_.equality_registry[found->second].lit.value;
  if (lit == 0 || !assignment_view_.attached()) return InterfaceEqualityAssignment::Unknown;
  const int assigned = assignment_view_.literal(lit < 0 ? -lit : lit);
  if (assigned == 0) return InterfaceEqualityAssignment::Unknown;
  return assigned == lit ? InterfaceEqualityAssignment::True : InterfaceEqualityAssignment::False;
}

std::vector<InterfaceEqualityRegistryEntry>
TheoryCombinationManager::interface_equalities_with_assignment(
    InterfaceEqualityAssignment assignment) const {
  std::vector<InterfaceEqualityRegistryEntry> entries;
  entries.reserve(summary_.equality_registry.size());
  for (const InterfaceEqualityRegistryEntry& entry : summary_.equality_registry) {
    if (interface_equality_assignment(entry.lhs, entry.rhs) == assignment) {
      entries.push_back(entry);
    }
  }
  return entries;
}

bool TheoryCombinationManager::mark_interface_equality_classification(
    core::TermId lhs, core::TermId rhs, InterfaceEqualityClassification classification) {
  const auto found = equality_index_.find(equality_key(lhs, rhs));
  if (found == equality_index_.end()) {
    return false;
  }
  summary_.equality_registry[found->second].classifications |= classification_bit(classification);
  return true;
}

bool TheoryCombinationManager::mark_interface_equality_classification(
    core::TermId equality, InterfaceEqualityClassification classification) {
  const core::Term& term = terms_.get(equality);
  if (term.kind != core::TermKind::Equality || term.children.size() != 2) {
    return false;
  }
  return mark_interface_equality_classification(term.children.at(0), term.children.at(1),
                                                classification);
}

bool TheoryCombinationManager::has_interface_equality_classification(
    core::TermId lhs, core::TermId rhs, InterfaceEqualityClassification classification) const {
  const auto found = equality_index_.find(equality_key(lhs, rhs));
  if (found == equality_index_.end()) {
    return false;
  }
  return (summary_.equality_registry[found->second].classifications &
          classification_bit(classification)) != 0;
}

void TheoryCombinationManager::clear_interface_equality_classifications() {
  for (InterfaceEqualityRegistryEntry& entry : summary_.equality_registry) {
    entry.classifications = 0;
  }
}

void TheoryCombinationManager::push_level() { value_level_starts_.push_back(value_trail_.size()); }

void TheoryCombinationManager::pop_to_level(std::size_t new_level) {
  invalidate_shared_model();
  const std::size_t keep_values =
      new_level < value_level_starts_.size() ? value_level_starts_[new_level] : std::size_t{0};
  while (value_trail_.size() > keep_values) {
    const EqualityValueTrailEntry entry = value_trail_.back();
    value_trail_.pop_back();
    if (entry.index < summary_.equality_registry.size()) {
      summary_.equality_registry[entry.index].value = entry.old_value;
    }
  }
  value_level_starts_.resize(std::min<std::size_t>(new_level, value_level_starts_.size()));
}

}  // namespace primo::smt
