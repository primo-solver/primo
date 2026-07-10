#pragma once

#include <optional>
#include <vector>

#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "sat/lit.hpp"
#include "theory/equality/union_find.hpp"

namespace primo::theory::equality {

class ExplainedCongruenceClosure {
 public:
  explicit ExplainedCongruenceClosure(const core::TermManager& terms);

  void add_term(core::TermId term);
  void assert_equal(core::TermId lhs, core::TermId rhs, std::vector<sat::Lit> reason);
  bool are_equal(core::TermId lhs, core::TermId rhs);
  std::optional<std::vector<sat::Lit>> explain_equal(core::TermId lhs, core::TermId rhs);

 private:
  struct Edge {
    core::TermId lhs = core::kInvalidTermId;
    core::TermId rhs = core::kInvalidTermId;
    std::vector<sat::Lit> reason;
  };

  void saturate();
  bool congruent(core::TermId lhs, core::TermId rhs) const;
  void add_edge(core::TermId lhs, core::TermId rhs, std::vector<sat::Lit> reason);
  std::optional<std::vector<sat::Lit>> explain_equal_without_saturation(core::TermId lhs,
                                                                        core::TermId rhs) const;
  std::vector<sat::Lit> congruence_reason(core::TermId lhs, core::TermId rhs) const;

  const core::TermManager& terms_;
  UnionFind classes_;
  std::vector<core::TermId> terms_seen_;
  std::vector<bool> seen_;
  std::vector<Edge> edges_;
  bool dirty_ = false;
};

}  // namespace primo::theory::equality
