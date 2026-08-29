#include "smt/formula_analysis.hpp"

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/term.hpp"
#include "core/term_manager.hpp"

namespace primo::smt {

bool contains_application_term(const core::TermManager& terms, core::TermId root) {
  std::vector<core::TermId> pending{root};
  while (!pending.empty()) {
    const core::TermId current = pending.back();
    pending.pop_back();
    const core::Term& term = terms.get(current);
    if (term.kind == core::TermKind::Application) {
      return true;
    }
    pending.insert(pending.end(), term.children.begin(), term.children.end());
  }
  return false;
}

FormulaFeatureSummary& FormulaFeatureSummary::operator|=(const FormulaFeatureSummary& other) {
  has_application_term |= other.has_application_term;
  has_equality |= other.has_equality;
  has_arithmetic |= other.has_arithmetic;
  has_euf_predicate |= other.has_euf_predicate;
  return *this;
}

// Adding a field must update operator|=. This tripwire protects solver
// dispatch: omitting application propagation has caused a wrong answer before.
static_assert(sizeof(FormulaFeatureSummary) == 4);

FormulaFeatureSummary summarize_formula_features(
    const core::TermManager& terms, core::TermId root,
    std::unordered_map<core::TermId, FormulaFeatureSummary>& memo, MixedDispatchMode dispatch) {
  const auto found = memo.find(root);
  if (found != memo.end()) {
    return found->second;
  }

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

    FormulaFeatureSummary summary;
    summary.has_application_term = term.kind == core::TermKind::Application;
    summary.has_euf_predicate =
        (term.kind == core::TermKind::Application ||
         (dispatch == MixedDispatchMode::Wide && term.kind == core::TermKind::Constant)) &&
        term.sort == terms.symbols().bool_sort();
    summary.has_arithmetic = term.kind == core::TermKind::Le || term.kind == core::TermKind::Lt;
    for (const core::TermId child : term.children) {
      summary |= memo.at(child);
    }
    if (term.kind == core::TermKind::Equality) {
      const bool real_equality = terms.get(term.children.at(0)).sort == terms.symbols().real_sort();
      summary.has_arithmetic |= real_equality;
      summary.has_equality = !real_equality || summary.has_application_term;
    }
    memo.emplace(visit.term, summary);
  }
  return memo.at(root);
}

namespace {

void collect_boolean_shape(const core::TermManager& terms, core::TermId root,
                           std::unordered_set<core::TermId>& seen, BooleanShape& shape) {
  std::vector<core::TermId> pending{root};
  while (!pending.empty()) {
    const core::TermId current = pending.back();
    pending.pop_back();
    if (!seen.insert(current).second) {
      continue;
    }
    const core::Term& term = terms.get(current);
    shape.or_nodes += term.kind == core::TermKind::Or ? 1U : 0U;
    shape.not_nodes += term.kind == core::TermKind::Not ? 1U : 0U;
    pending.insert(pending.end(), term.children.begin(), term.children.end());
  }
}

void collect_top_level_or_shape(const core::TermManager& terms, core::TermId root,
                                BooleanShape& shape) {
  std::vector<core::TermId> pending{root};
  std::unordered_set<core::TermId> seen;
  while (!pending.empty()) {
    const core::TermId current = pending.back();
    pending.pop_back();
    if (!seen.insert(current).second) {
      continue;
    }
    const core::Term& term = terms.get(current);
    if (term.kind == core::TermKind::And) {
      pending.insert(pending.end(), term.children.begin(), term.children.end());
    } else if (term.kind == core::TermKind::Or) {
      ++shape.top_level_or_nodes;
      shape.max_top_level_or_children =
          std::max(shape.max_top_level_or_children, term.children.size());
    }
  }
}

}  // namespace

BooleanShape boolean_shape(const core::TermManager& terms,
                           const std::vector<core::TermId>& assertions) {
  BooleanShape shape;
  std::unordered_set<core::TermId> seen;
  for (const core::TermId assertion : assertions) {
    collect_boolean_shape(terms, assertion, seen, shape);
    collect_top_level_or_shape(terms, assertion, shape);
  }
  return shape;
}

bool should_use_auto_top_level_or_tseitin(const CheckSatOptions& options,
                                          const FiniteDomainAtMostOneResult& finite_domain,
                                          const BooleanShape& shape) {
  constexpr std::size_t kMinTopLevelOrNodes = 1000;
  constexpr std::size_t kMaxTopLevelOrNodes = 2500;
  constexpr std::size_t kMaxNotNodes = 700;
  return options.auto_top_level_or_tseitin && !options.top_level_or_tseitin &&
         finite_domain.choices != 0 && !finite_domain.clauses.empty() &&
         shape.top_level_or_nodes >= kMinTopLevelOrNodes &&
         shape.top_level_or_nodes <= kMaxTopLevelOrNodes && shape.not_nodes <= kMaxNotNodes;
}

}  // namespace primo::smt
