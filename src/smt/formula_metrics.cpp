#include "smt/formula_metrics.hpp"

#include <cstddef>
#include <vector>

#include "core/term.hpp"
#include "core/term_manager.hpp"

namespace primo::smt {

std::size_t reachable_term_count(const core::TermManager& terms,
                                 const std::vector<core::TermId>& roots) {
  std::vector<unsigned char> seen(terms.size(), 0);
  std::size_t count = 0;
  std::vector<core::TermId> work = roots;
  while (!work.empty()) {
    const core::TermId term_id = work.back();
    work.pop_back();
    if (term_id >= seen.size()) {
      seen.resize(term_id + 1, 0);
    }
    if (seen[term_id] != 0) {
      continue;
    }
    seen[term_id] = 1;
    ++count;
    const core::Term& term = terms.get(term_id);
    work.insert(work.end(), term.children.begin(), term.children.end());
  }
  return count;
}

}  // namespace primo::smt
