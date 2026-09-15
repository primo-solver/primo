#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "core/ids.hpp"

namespace primo::core {

class TermManager;

// Index arithmetic for de Bruijn bound variables (docs/quantifiers.md). These
// are the only operations that compute indices; every other component treats
// a BoundVariable as an ordinary leaf. All traversals are iterative, memoized
// over the DAG, and skip any subterm whose loose index shows it is unaffected,
// so closed subterms cost O(1).

// Structure of a Forall/Exists node. Each throws std::invalid_argument for any
// other kind. Results are copies, safe to keep across term creation.
std::uint32_t quantifier_arity(const TermManager& terms, TermId quantifier);
TermId quantifier_body(const TermManager& terms, TermId quantifier);
// Variable sorts in declaration order: the last one has index 0 in the body.
std::vector<SortId> quantifier_sorts(const TermManager& terms, TermId quantifier);
std::vector<TermId> quantifier_patterns(const TermManager& terms, TermId quantifier);

// Adds `amount` to every bound-variable index that is loose in `term`, i.e.
// moves the term under `amount` more binders. The identity for closed terms
// and for amount 0.
TermId shift(TermManager& terms, TermId term, std::uint32_t amount);

// Opens a Forall/Exists node: returns its body with the node's variables
// replaced by `replacements`, given in declaration order, and every index
// that referred past the node lowered by its arity. A replacement is read at
// the node's own position -- it may mention variables bound further out, as a
// Skolem term does -- and is shifted as it passes the body's binders.
// Patterns are dropped. Sorts must match the declarations.
TermId instantiate(TermManager& terms, TermId quantifier, std::span<const TermId> replacements);

// Rebuilds an operator node (anything but a leaf, Forall or Exists) over new
// children through the ordinary constructors, which may simplify. Throws
// std::invalid_argument for leaves and quantifiers.
TermId rebuild_operator(TermManager& terms, TermId original, std::vector<TermId> children);

// Debug check: throws std::logic_error unless every bound variable under
// `root` refers to an enclosing binder of its recorded sort and every
// quantifier's declarations are well formed. `enclosing` lists the sorts of
// the variables bound around `root`, innermost last.
void check_bound_variables(const TermManager& terms, TermId root,
                           std::span<const SortId> enclosing = {});

}  // namespace primo::core
