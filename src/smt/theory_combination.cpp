#include "smt/theory_combination.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"

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

bool is_internal_symbol_name(const std::string& name) { return name.starts_with("$primo_"); }

bool is_interface_constant(const core::TermManager& terms, core::TermId term_id) {
  const core::Term& term = terms.get(term_id);
  if (term.kind != core::TermKind::Constant || term.sort == terms.symbols().bool_sort()) {
    return false;
  }
  return !is_internal_symbol_name(terms.symbols().get_symbol(term.symbol).name);
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

void collect_real_terms_in_euf_atom(
    const core::TermManager& terms, core::TermId term_id, core::TermId atom_id,
    NelsonOppenInterfaceSummary& summary,
    std::unordered_set<PurificationSiteKey, PurificationSiteKeyHash>& seen) {
  const core::Term& term = terms.get(term_id);
  if (term.sort == terms.symbols().real_sort()) {
    append_purification_site(summary, seen, PurificationSiteKind::RealTermInEufAtom, term_id,
                             atom_id, term.sort);
  }
  for (const core::TermId child : term.children) {
    collect_real_terms_in_euf_atom(terms, child, atom_id, summary, seen);
  }
}

void collect_uninterpreted_applications_in_arithmetic_atom(
    const core::TermManager& terms, core::TermId term_id, core::TermId atom_id,
    NelsonOppenInterfaceSummary& summary,
    std::unordered_set<PurificationSiteKey, PurificationSiteKeyHash>& seen) {
  const core::Term& term = terms.get(term_id);
  if (term.kind == core::TermKind::Application) {
    append_purification_site(summary, seen,
                             PurificationSiteKind::UninterpretedApplicationInArithmeticAtom,
                             term_id, atom_id, term.sort);
  }
  for (const core::TermId child : term.children) {
    collect_uninterpreted_applications_in_arithmetic_atom(terms, child, atom_id, summary, seen);
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

void TheoryCombinationManager::rebuild_from_interface_terms(
    const std::vector<core::TermId>& interface_terms) {
  summary_ = NelsonOppenInterfaceSummary{};
  equality_index_.clear();
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
    }
    for (std::size_t i = 0; i < sort_terms.size(); ++i) {
      for (std::size_t j = i + 1; j < sort_terms.size(); ++j) {
        const TermPair pair = canonical_pair(sort_terms[i], sort_terms[j]);
        summary_.equality_registry.push_back(
            InterfaceEqualityRegistryEntry{core::kInvalidTermId, pair.lhs, pair.rhs, sort,
                                           sat::Lit{0}, InterfaceEqualityStatus::Missing});
        ++summary_.missing_equalities;
      }
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

void TheoryCombinationManager::rebuild(const std::vector<core::TermId>& assertions) {
  summary_ = NelsonOppenInterfaceSummary{};
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
    }
    for (std::size_t i = 0; i < sort_terms.size(); ++i) {
      for (std::size_t j = i + 1; j < sort_terms.size(); ++j) {
        const TermPair pair = canonical_pair(sort_terms[i], sort_terms[j]);
        const auto visible = visible_pairs.find(pair);
        if (visible == visible_pairs.end()) {
          summary_.equality_registry.push_back(
              InterfaceEqualityRegistryEntry{core::kInvalidTermId, pair.lhs, pair.rhs, sort,
                                             sat::Lit{0}, InterfaceEqualityStatus::Missing});
          ++summary_.missing_equalities;
          continue;
        }
        summary_.equality_registry.push_back(
            InterfaceEqualityRegistryEntry{visible->second, pair.lhs, pair.rhs, sort, sat::Lit{0},
                                           InterfaceEqualityStatus::Visible});
      }
    }
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
  summary_.equality_registry[found->second].value = value;
  return true;
}

void TheoryCombinationManager::clear_interface_equality_values() {
  for (InterfaceEqualityRegistryEntry& entry : summary_.equality_registry) {
    entry.value = InterfaceEqualityValue::Unknown;
  }
}

}  // namespace primo::smt
