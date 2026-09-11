#include "core/binders.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "util/rational.hpp"

namespace primo::core {

namespace {

const Term& require_quantifier(const TermManager& terms, TermId quantifier) {
  const Term& node = terms.get(quantifier);
  if (node.kind != TermKind::Forall && node.kind != TermKind::Exists) {
    throw std::invalid_argument("expected a Forall or Exists term");
  }
  return node;
}

std::uint64_t memo_key(TermId term, std::uint32_t cutoff) {
  return (static_cast<std::uint64_t>(term) << 32U) | cutoff;
}

// Rebuilds a non-binder node from rewritten children through the ordinary
// constructors, so the result is hash-consed and sort-checked exactly like a
// term built from syntax.
TermId rebuild(TermManager& terms, TermKind kind, SymbolId symbol, const util::Rational& rational,
               std::vector<TermId> children) {
  switch (kind) {
    case TermKind::Application:
      return terms.mk_app(symbol, std::move(children));
    case TermKind::Equality:
      return terms.mk_eq(children.at(0), children.at(1));
    case TermKind::Not:
      return terms.mk_not(children.at(0));
    case TermKind::And:
      return terms.mk_and(std::move(children));
    case TermKind::Or:
      return terms.mk_or(std::move(children));
    case TermKind::Implies:
      return terms.mk_implies(children.at(0), children.at(1));
    case TermKind::Plus:
      return terms.mk_plus(std::move(children));
    case TermKind::Times:
      return terms.mk_times(rational, children.at(0));
    case TermKind::Le:
      return terms.mk_le(children.at(0), children.at(1));
    case TermKind::Lt:
      return terms.mk_lt(children.at(0), children.at(1));
    case TermKind::Ite:
      return terms.mk_ite(children.at(0), children.at(1), children.at(2));
    case TermKind::IntDiv:
      return terms.mk_int_div(rational, children.at(0));
    case TermKind::IntMod:
      return terms.mk_int_mod(rational, children.at(0));
    case TermKind::Abs:
      return terms.mk_abs(children.at(0));
    case TermKind::Pattern:
      return terms.mk_pattern(std::move(children));
    case TermKind::BoolValue:
    case TermKind::Constant:
    case TermKind::Rational:
    case TermKind::BoundVariable:
    case TermKind::Forall:
    case TermKind::Exists:
      break;
  }
  throw std::logic_error("binder rewrite reached a leaf or binder as an operator");
}

// Rewrites every bound variable that is loose at the root, passing it the
// number of binders crossed on the way down (the cutoff). Subterms whose loose
// index is at most the cutoff contain no such variable and are returned as
// they are. Declaration children are never visited.
using VariableRewrite =
    std::function<TermId(std::uint32_t index, SortId sort, std::uint32_t cutoff)>;

TermId rewrite_loose(TermManager& terms, TermId root, const VariableRewrite& rewrite) {
  struct Frame {
    TermId term = kInvalidTermId;
    std::uint32_t cutoff = 0;
    bool expanded = false;
  };
  std::unordered_map<std::uint64_t, TermId> memo;
  std::vector<Frame> stack{{root, 0, false}};
  while (!stack.empty()) {
    const Frame frame = stack.back();
    const std::uint64_t key = memo_key(frame.term, frame.cutoff);
    if (memo.contains(key)) {
      stack.pop_back();
      continue;
    }
    if (terms.loose_index(frame.term) <= frame.cutoff) {
      memo.emplace(key, frame.term);
      stack.pop_back();
      continue;
    }
    // Snapshot everything needed: rewriting creates terms, which may
    // reallocate the arena under a borrowed reference.
    const Term& node = terms.get(frame.term);
    const TermKind kind = node.kind;
    const SymbolId symbol = node.symbol;
    const SortId sort = node.sort;
    if (kind == TermKind::BoundVariable) {
      stack.pop_back();
      memo.emplace(key, rewrite(symbol, sort, frame.cutoff));
      continue;
    }
    const bool binder = kind == TermKind::Forall || kind == TermKind::Exists;
    const std::uint32_t arity = binder ? symbol : 0;
    const std::uint32_t child_cutoff = frame.cutoff + arity;
    const std::vector<TermId> children = node.children;
    const util::Rational rational = node.rational_value;
    if (!frame.expanded) {
      stack.back().expanded = true;
      for (std::size_t position = arity; position < children.size(); ++position) {
        stack.push_back({children[position], child_cutoff, false});
      }
      continue;
    }
    stack.pop_back();
    std::vector<TermId> rewritten;
    rewritten.reserve(children.size() - arity);
    for (std::size_t position = arity; position < children.size(); ++position) {
      rewritten.push_back(memo.at(memo_key(children[position], child_cutoff)));
    }
    TermId result = kInvalidTermId;
    if (binder) {
      std::vector<SortId> sorts;
      sorts.reserve(arity);
      for (std::uint32_t position = 0; position < arity; ++position) {
        sorts.push_back(terms.get(children[position]).sort);
      }
      std::vector<TermId> patterns(rewritten.begin() + 1, rewritten.end());
      result = kind == TermKind::Forall
                   ? terms.mk_forall(sorts, rewritten.front(), std::move(patterns))
                   : terms.mk_exists(sorts, rewritten.front(), std::move(patterns));
    } else {
      result = rebuild(terms, kind, symbol, rational, std::move(rewritten));
    }
    memo.emplace(key, result);
  }
  return memo.at(memo_key(root, 0));
}

}  // namespace

std::uint32_t quantifier_arity(const TermManager& terms, TermId quantifier) {
  return require_quantifier(terms, quantifier).symbol;
}

TermId quantifier_body(const TermManager& terms, TermId quantifier) {
  const Term& node = require_quantifier(terms, quantifier);
  return node.children.at(node.symbol);
}

std::vector<SortId> quantifier_sorts(const TermManager& terms, TermId quantifier) {
  const Term& node = require_quantifier(terms, quantifier);
  std::vector<SortId> sorts;
  sorts.reserve(node.symbol);
  for (std::uint32_t position = 0; position < node.symbol; ++position) {
    sorts.push_back(terms.get(node.children.at(position)).sort);
  }
  return sorts;
}

std::vector<TermId> quantifier_patterns(const TermManager& terms, TermId quantifier) {
  const Term& node = require_quantifier(terms, quantifier);
  return {node.children.begin() + node.symbol + 1, node.children.end()};
}

TermId shift(TermManager& terms, TermId term, std::uint32_t amount) {
  if (amount == 0 || terms.loose_index(term) == 0) {
    return term;
  }
  return rewrite_loose(terms, term,
                       [&terms, amount](std::uint32_t index, SortId sort, std::uint32_t) {
                         if (static_cast<std::uint64_t>(index) + amount >= kInvalidSymbolId - 1) {
                           throw std::invalid_argument("bound variable index out of range");
                         }
                         return terms.mk_bound_variable(index + amount, sort);
                       });
}

TermId instantiate(TermManager& terms, TermId quantifier, std::span<const TermId> replacements) {
  const std::uint32_t arity = quantifier_arity(terms, quantifier);
  if (replacements.size() != arity) {
    throw std::invalid_argument("instantiation needs one term per bound variable");
  }
  const std::vector<SortId> sorts = quantifier_sorts(terms, quantifier);
  for (std::uint32_t position = 0; position < arity; ++position) {
    if (terms.get(replacements[position]).sort != sorts[position]) {
      throw std::invalid_argument("instantiation term sort mismatch");
    }
  }
  const TermId body = quantifier_body(terms, quantifier);
  const std::vector<TermId> owned(replacements.begin(), replacements.end());
  std::unordered_map<std::uint64_t, TermId> shifted;
  return rewrite_loose(terms, body,
                       [&](std::uint32_t index, SortId sort, std::uint32_t cutoff) -> TermId {
                         const std::uint32_t offset = index - cutoff;
                         if (offset >= arity) {
                           return terms.mk_bound_variable(index - arity, sort);
                         }
                         const std::uint32_t position = arity - 1 - offset;
                         if (sort != sorts[position]) {
                           throw std::logic_error("bound variable sort does not match its binder");
                         }
                         const std::uint64_t key = memo_key(position, cutoff);
                         if (const auto found = shifted.find(key); found != shifted.end()) {
                           return found->second;
                         }
                         const TermId result = shift(terms, owned[position], cutoff);
                         shifted.emplace(key, result);
                         return result;
                       });
}

void check_bound_variables(const TermManager& terms, TermId root,
                           std::span<const SortId> enclosing) {
  // A subterm's correctness depends only on the sorts of the binders its loose
  // variables reach, so a frame carries just that suffix of the context, and
  // (term, suffix) pairs are checked once.
  struct Frame {
    TermId term = kInvalidTermId;
    std::vector<SortId> context;
  };
  std::set<std::pair<TermId, std::vector<SortId>>> checked;
  std::vector<Frame> stack;
  stack.push_back({root, std::vector<SortId>(enclosing.begin(), enclosing.end())});
  while (!stack.empty()) {
    Frame frame = std::move(stack.back());
    stack.pop_back();
    const std::uint32_t loose = terms.loose_index(frame.term);
    if (loose > frame.context.size()) {
      throw std::logic_error("bound variable refers past its enclosing binders");
    }
    if (loose == 0 && !terms.contains_quantifier(frame.term)) {
      continue;
    }
    std::vector<SortId> relevant(frame.context.end() - loose, frame.context.end());
    if (!checked.emplace(frame.term, relevant).second) {
      continue;
    }
    const Term& node = terms.get(frame.term);
    if (node.kind == TermKind::BoundVariable) {
      if (node.sort != relevant[relevant.size() - 1 - node.symbol]) {
        throw std::logic_error("bound variable sort does not match its binder");
      }
      continue;
    }
    if (node.kind == TermKind::Forall || node.kind == TermKind::Exists) {
      const std::uint32_t arity = node.symbol;
      if (arity == 0 || node.children.size() < arity + std::size_t{1}) {
        throw std::logic_error("malformed quantifier");
      }
      std::vector<SortId> inner = relevant;
      for (std::uint32_t position = 0; position < arity; ++position) {
        const Term& declaration = terms.get(node.children[position]);
        if (declaration.kind != TermKind::BoundVariable ||
            declaration.symbol != arity - 1 - position) {
          throw std::logic_error("malformed quantifier declarations");
        }
        inner.push_back(declaration.sort);
      }
      for (std::size_t position = arity; position < node.children.size(); ++position) {
        stack.push_back({node.children[position], inner});
      }
      continue;
    }
    for (const TermId child : node.children) {
      stack.push_back({child, relevant});
    }
  }
}

}  // namespace primo::core
