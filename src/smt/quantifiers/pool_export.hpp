#pragma once

#include <span>
#include <string>

#include "core/ids.hpp"

namespace primo::core {
class TermManager;
}

namespace primo::theory {
struct QuantifierExport;
}

namespace primo::util {
class Statistics;
}

namespace primo::smt {

// Writes what enumerative instantiation used as SMT-LIB, for checking an
// answer with another solver's pool-based instantiation
// (scripts/quantifier_pool_check.py):
//   - declarations of the constants the quantifier component created;
//   - its Skolem lemmas, as assertions;
//   - one cvc5 pool per variable-sort signature holding the tuples whose
//     instances became lemmas, each announced by `; primo-pool NAME SORT...`.
// A Skolemization of a top-level assertion of `roots` -- an Exists root, or a
// Not over a Forall root -- is announced before its lemma by
//   `; primo-skolem-root KIND OCCURRENCE :sorts S... :constants C... :lemma next|none`
// where KIND is exists or not-forall and OCCURRENCE counts, from 0, the roots
// of that kind and variable sorts in order. A checker can then Skolemize the
// input's own assertion with these constants instead of trusting the lemma's
// printing of it.
// Terms mentioning another internal constant (named Boolean arguments,
// lowering auxiliaries) cannot be printed in the input's signature; such a
// lemma or tuple is skipped and counted. Each addition is a declaration, a
// pool, or a Skolem lemma naming fresh constants, so adding the file to the
// input keeps a satisfiable problem satisfiable. Throws std::runtime_error when
// the file cannot be written.
void write_pool_export(const core::TermManager& terms, std::span<const core::TermId> roots,
                       const theory::QuantifierExport& record, const std::string& path,
                       util::Statistics& statistics);

}  // namespace primo::smt
