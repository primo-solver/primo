#include "theory/equality/explained_congruence_closure.hpp"

#include <algorithm>
#include <cstddef>
#include <queue>
#include <stdexcept>

namespace primo::theory::equality {

namespace {

void append_unique(std::vector<sat::Lit>& target, const std::vector<sat::Lit>& source) {
  for (const sat::Lit lit : source) {
    const auto duplicate = std::find_if(target.begin(), target.end(), [&](sat::Lit existing) {
      return existing.value == lit.value;
    });
    if (duplicate == target.end()) {
      target.push_back(lit);
    }
  }
}

}  // namespace

ExplainedCongruenceClosure::ExplainedCongruenceClosure(const core::TermManager& terms)
    : terms_(terms), classes_(terms.size()), seen_(terms.size(), false) {}

void ExplainedCongruenceClosure::add_term(core::TermId term) {
  terms_.get(term);
  if (term >= seen_.size()) {
    throw std::logic_error("explained congruence closure cannot grow after construction");
  }
  if (seen_[term]) {
    return;
  }
  seen_[term] = true;
  terms_seen_.push_back(term);
  for (const core::TermId child : terms_.get(term).children) {
    add_term(child);
  }
  dirty_ = true;
}

void ExplainedCongruenceClosure::assert_equal(core::TermId lhs, core::TermId rhs,
                                              std::vector<sat::Lit> reason) {
  add_term(lhs);
  add_term(rhs);
  add_edge(lhs, rhs, std::move(reason));
}

bool ExplainedCongruenceClosure::are_equal(core::TermId lhs, core::TermId rhs) {
  add_term(lhs);
  add_term(rhs);
  saturate();
  return classes_.same(lhs, rhs);
}

std::optional<std::vector<sat::Lit>> ExplainedCongruenceClosure::explain_equal(core::TermId lhs,
                                                                               core::TermId rhs) {
  add_term(lhs);
  add_term(rhs);
  saturate();
  if (!classes_.same(lhs, rhs)) {
    return std::nullopt;
  }
  return explain_equal_without_saturation(lhs, rhs);
}

void ExplainedCongruenceClosure::saturate() {
  // Nieuwenhuis and Oliveras describe explanation-producing congruence closure
  // using proof forests. This scaffold keeps the same proof obligation but uses
  // a small explicit equality graph: every explicit or congruence merge stores
  // the SAT literals that justify it, and explanations are graph paths.
  while (dirty_) {
    dirty_ = false;
    for (std::size_t i = 0; i < terms_seen_.size(); ++i) {
      for (std::size_t j = i + 1; j < terms_seen_.size(); ++j) {
        const core::TermId lhs = terms_seen_[i];
        const core::TermId rhs = terms_seen_[j];
        if (!classes_.same(lhs, rhs) && congruent(lhs, rhs)) {
          add_edge(lhs, rhs, congruence_reason(lhs, rhs));
        }
      }
    }
  }
}

bool ExplainedCongruenceClosure::congruent(core::TermId lhs, core::TermId rhs) const {
  const core::Term& lhs_term = terms_.get(lhs);
  const core::Term& rhs_term = terms_.get(rhs);
  if (lhs_term.kind != core::TermKind::Application ||
      rhs_term.kind != core::TermKind::Application) {
    return false;
  }
  if (lhs_term.symbol != rhs_term.symbol || lhs_term.children.size() != rhs_term.children.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs_term.children.size(); ++i) {
    if (!classes_.same(lhs_term.children[i], rhs_term.children[i])) {
      return false;
    }
  }
  return true;
}

void ExplainedCongruenceClosure::add_edge(core::TermId lhs, core::TermId rhs,
                                          std::vector<sat::Lit> reason) {
  if (lhs == rhs) {
    return;
  }
  edges_.push_back(Edge{lhs, rhs, std::move(reason)});
  dirty_ = classes_.unite(lhs, rhs) || dirty_;
}

std::optional<std::vector<sat::Lit>> ExplainedCongruenceClosure::explain_equal_without_saturation(
    core::TermId lhs, core::TermId rhs) const {
  if (lhs == rhs) {
    return std::vector<sat::Lit>{};
  }
  if (lhs >= seen_.size() || rhs >= seen_.size() || !seen_[lhs] || !seen_[rhs]) {
    return std::nullopt;
  }

  std::vector<bool> visited(seen_.size(), false);
  std::vector<std::size_t> parent_edge(seen_.size(), edges_.size());
  std::vector<core::TermId> parent(seen_.size(), core::kInvalidTermId);
  std::queue<core::TermId> queue;
  visited[lhs] = true;
  queue.push(lhs);

  while (!queue.empty() && !visited[rhs]) {
    const core::TermId current = queue.front();
    queue.pop();
    for (std::size_t edge_index = 0; edge_index < edges_.size(); ++edge_index) {
      const Edge& edge = edges_[edge_index];
      core::TermId next = core::kInvalidTermId;
      if (edge.lhs == current) {
        next = edge.rhs;
      } else if (edge.rhs == current) {
        next = edge.lhs;
      }
      if (next == core::kInvalidTermId || next >= visited.size() || visited[next]) {
        continue;
      }
      visited[next] = true;
      parent[next] = current;
      parent_edge[next] = edge_index;
      queue.push(next);
    }
  }

  if (!visited[rhs]) {
    return std::nullopt;
  }
  std::vector<sat::Lit> reason;
  for (core::TermId current = rhs; current != lhs; current = parent[current]) {
    append_unique(reason, edges_[parent_edge[current]].reason);
  }
  return reason;
}

std::vector<sat::Lit> ExplainedCongruenceClosure::congruence_reason(core::TermId lhs,
                                                                    core::TermId rhs) const {
  const core::Term& lhs_term = terms_.get(lhs);
  const core::Term& rhs_term = terms_.get(rhs);
  std::vector<sat::Lit> reason;
  for (std::size_t i = 0; i < lhs_term.children.size(); ++i) {
    std::optional<std::vector<sat::Lit>> child_reason =
        explain_equal_without_saturation(lhs_term.children[i], rhs_term.children[i]);
    if (child_reason.has_value()) {
      append_unique(reason, child_reason.value());
    }
  }
  return reason;
}

}  // namespace primo::theory::equality
