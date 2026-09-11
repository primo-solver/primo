#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

#include "core/binders.hpp"
#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"

using primo::core::check_bound_variables;
using primo::core::instantiate;
using primo::core::kInvalidTermId;
using primo::core::quantifier_arity;
using primo::core::quantifier_body;
using primo::core::quantifier_patterns;
using primo::core::quantifier_sorts;
using primo::core::shift;
using primo::core::SortId;
using primo::core::SymbolId;
using primo::core::SymbolTable;
using primo::core::TermId;
using primo::core::TermKind;
using primo::core::TermManager;

namespace {

template <typename Exception, typename Fn>
void expect_throw(Fn fn) {
  try {
    fn();
  } catch (const Exception&) {
    return;
  }
  assert(false && "expected an exception");
}

struct Signature {
  SymbolTable symbols;
  SortId u = 0;
  SortId v = 0;
  SymbolId f = 0;  // U -> U
  SymbolId g = 0;  // U U -> U
  SymbolId p = 0;  // U U -> Bool
  SymbolId r = 0;  // U U U -> Bool
  SymbolId a = 0;
  SymbolId b = 0;
  SymbolId c = 0;  // Bool
  SymbolId d = 0;  // V

  Signature() {
    u = symbols.declare_sort("U");
    v = symbols.declare_sort("V");
    f = symbols.declare_function("f", {u}, u);
    g = symbols.declare_function("g", {u, u}, u);
    p = symbols.declare_predicate("p", {u, u});
    r = symbols.declare_predicate("r", {u, u, u});
    a = symbols.declare_constant("a", u);
    b = symbols.declare_constant("b", u);
    c = symbols.declare_constant("c", symbols.bool_sort());
    d = symbols.declare_constant("d", v);
  }
};

void constructors_hash_cons_and_validate() {
  Signature sig;
  TermManager terms(sig.symbols);
  const TermId a = terms.mk_constant(sig.a);
  const TermId fa = terms.mk_app(sig.f, {a});
  assert(terms.quantifier_count() == 0);
  assert(terms.loose_index(fa) == 0 && !terms.contains_quantifier(fa));

  const TermId x0 = terms.mk_bound_variable(0, sig.u);
  assert(x0 == terms.mk_bound_variable(0, sig.u));
  assert(terms.get(x0).kind == TermKind::BoundVariable);
  assert(terms.loose_index(x0) == 1);
  // Interned before the caches existed, and still correctly closed.
  assert(terms.loose_index(fa) == 0);

  const TermId body = terms.mk_app(sig.p, {x0, a});
  const TermId q = terms.mk_forall({sig.u}, body);
  assert(q == terms.mk_forall({sig.u}, body));
  assert(terms.quantifier_count() == 1);
  assert(terms.loose_index(q) == 0 && terms.contains_quantifier(q));
  assert(!terms.contains_quantifier(body));
  assert(terms.mk_exists({sig.u}, body) != q);
  assert(quantifier_arity(terms, q) == 1 && quantifier_body(terms, q) == body);
  assert(quantifier_sorts(terms, q) == std::vector<SortId>{sig.u});
  assert(quantifier_patterns(terms, q).empty());

  // The sort of an unused variable is part of the term.
  const TermId c = terms.mk_constant(sig.c);
  assert(terms.mk_forall({sig.u}, c) != terms.mk_forall({sig.v}, c));

  expect_throw<std::invalid_argument>([&] { static_cast<void>(terms.mk_forall({}, body)); });
  expect_throw<std::invalid_argument>([&] { static_cast<void>(terms.mk_forall({sig.u}, x0)); });
  expect_throw<std::out_of_range>([&] { static_cast<void>(terms.mk_bound_variable(0, 12345)); });
  expect_throw<std::invalid_argument>([&] { static_cast<void>(terms.mk_pattern({q})); });
  expect_throw<std::invalid_argument>(
      [&] { static_cast<void>(terms.mk_forall({sig.u}, body, {body})); });
  expect_throw<std::invalid_argument>([&] { static_cast<void>(quantifier_body(terms, body)); });

  const TermId pattern = terms.mk_pattern({terms.mk_app(sig.f, {x0})});
  const TermId with_pattern = terms.mk_forall({sig.u}, body, {pattern});
  assert(with_pattern != q);
  assert(quantifier_patterns(terms, with_pattern) == std::vector<TermId>{pattern});
  assert(terms.loose_index(with_pattern) == 0);
  assert(instantiate(terms, with_pattern, std::vector<TermId>{a}) == terms.mk_app(sig.p, {a, a}));
}

void loose_indices_track_unbound_variables() {
  Signature sig;
  TermManager terms(sig.symbols);
  const auto var = [&](std::uint32_t index) { return terms.mk_bound_variable(index, sig.u); };
  assert(terms.loose_index(terms.mk_app(sig.g, {var(0), var(2)})) == 3);
  assert(terms.loose_index(
             terms.mk_forall({sig.u, sig.u}, terms.mk_app(sig.p, {var(1), var(0)}))) == 0);
  const TermId open = terms.mk_forall({sig.u}, terms.mk_app(sig.p, {var(0), var(1)}));
  assert(terms.loose_index(open) == 1);
  const TermId inner = terms.mk_forall({sig.u}, terms.mk_app(sig.r, {var(0), var(1), var(2)}));
  assert(terms.loose_index(inner) == 2);
  const TermId outer = terms.mk_forall({sig.u}, inner);
  assert(terms.loose_index(outer) == 1);
  assert(terms.contains_quantifier(terms.mk_and({outer, terms.mk_constant(sig.c)})));
  assert(terms.quantifier_count() == 4);
}

void shift_satisfies_its_laws() {
  Signature sig;
  TermManager terms(sig.symbols);
  const auto var = [&](std::uint32_t index) { return terms.mk_bound_variable(index, sig.u); };
  const TermId t = terms.mk_app(sig.g, {var(0), var(2)});
  assert(shift(terms, t, 0) == t);
  const TermId closed = terms.mk_app(sig.f, {terms.mk_constant(sig.a)});
  assert(shift(terms, closed, 5) == closed);
  assert(shift(terms, shift(terms, t, 1), 2) == shift(terms, t, 3));
  assert(shift(terms, t, 3) == terms.mk_app(sig.g, {var(3), var(5)}));
  // Variables bound inside the term are not shifted.
  const TermId open = terms.mk_forall({sig.u}, terms.mk_app(sig.p, {var(0), var(1)}));
  assert(shift(terms, open, 2) == terms.mk_forall({sig.u}, terms.mk_app(sig.p, {var(0), var(3)})));
}

void instantiate_follows_the_design_note_examples() {
  Signature sig;
  TermManager terms(sig.symbols);
  const auto var = [&](std::uint32_t index) { return terms.mk_bound_variable(index, sig.u); };
  const TermId a = terms.mk_constant(sig.a);

  // Q = forall x. exists y. forall z. r(x, y, z): inside r, z=%0, y=%1, x=%2.
  const TermId matrix = terms.mk_app(sig.r, {var(2), var(1), var(0)});
  const TermId middle = terms.mk_exists({sig.u}, terms.mk_forall({sig.u}, matrix));
  const TermId q = terms.mk_forall({sig.u}, middle);
  check_bound_variables(terms, q);

  // Instantiating x := a keeps y and z as they are.
  const TermId instance = instantiate(terms, q, std::vector<TermId>{a});
  assert(
      instance ==
      terms.mk_exists({sig.u}, terms.mk_forall({sig.u}, terms.mk_app(sig.r, {a, var(1), var(0)}))));
  check_bound_variables(terms, instance);

  // Skolemizing y below x: at the exists node x is %0, so the Skolem term is
  // f(%0). Inside r, y becomes f(%1) and x drops from %2 to %1.
  assert(quantifier_body(terms, q) == middle);
  const TermId skolemized =
      instantiate(terms, middle, std::vector<TermId>{terms.mk_app(sig.f, {var(0)})});
  assert(skolemized ==
         terms.mk_forall({sig.u},
                         terms.mk_app(sig.r, {var(1), terms.mk_app(sig.f, {var(1)}), var(0)})));
  check_bound_variables(terms, skolemized, std::vector<SortId>{sig.u});
}

void instantiate_handles_lists_outer_variables_and_errors() {
  Signature sig;
  TermManager terms(sig.symbols);
  const auto var = [&](std::uint32_t index) { return terms.mk_bound_variable(index, sig.u); };
  const TermId a = terms.mk_constant(sig.a);
  const TermId b = terms.mk_constant(sig.b);

  // forall ((x U) (y U)) p(x, y): the last declared variable has index 0.
  const TermId pair = terms.mk_forall({sig.u, sig.u}, terms.mk_app(sig.p, {var(1), var(0)}));
  assert(instantiate(terms, pair, std::vector<TermId>{a, b}) == terms.mk_app(sig.p, {a, b}));

  // An index past the opened binder is lowered.
  const TermId open = terms.mk_forall({sig.u}, terms.mk_app(sig.p, {var(0), var(1)}));
  assert(instantiate(terms, open, std::vector<TermId>{a}) == terms.mk_app(sig.p, {a, var(0)}));

  // A replacement naming an outer variable is shifted under the inner binder:
  // forall x. forall z. p(x, z), with x := w where w is %0 outside.
  const TermId nested =
      terms.mk_forall({sig.u}, terms.mk_forall({sig.u}, terms.mk_app(sig.p, {var(1), var(0)})));
  assert(instantiate(terms, nested, std::vector<TermId>{var(0)}) ==
         terms.mk_forall({sig.u}, terms.mk_app(sig.p, {var(1), var(0)})));

  // Substitution may collapse structure through the ordinary constructors.
  const TermId diagonal = terms.mk_forall({sig.u, sig.u}, terms.mk_eq(var(1), var(0)));
  assert(instantiate(terms, diagonal, std::vector<TermId>{a, a}) == terms.mk_bool(true));

  const TermId d = terms.mk_constant(sig.d);
  expect_throw<std::invalid_argument>(
      [&] { static_cast<void>(instantiate(terms, pair, std::vector<TermId>{a})); });
  expect_throw<std::invalid_argument>(
      [&] { static_cast<void>(instantiate(terms, pair, std::vector<TermId>{a, d})); });
  expect_throw<std::invalid_argument>(
      [&] { static_cast<void>(instantiate(terms, a, std::vector<TermId>{})); });
}

void checker_rejects_ill_scoped_terms() {
  Signature sig;
  TermManager terms(sig.symbols);
  const auto var = [&](std::uint32_t index) { return terms.mk_bound_variable(index, sig.u); };
  const TermId dangling = terms.mk_forall({sig.u}, terms.mk_app(sig.p, {var(0), var(1)}));
  expect_throw<std::logic_error>([&] { check_bound_variables(terms, dangling); });
  check_bound_variables(terms, dangling, std::vector<SortId>{sig.u});
  expect_throw<std::logic_error>(
      [&] { check_bound_variables(terms, dangling, std::vector<SortId>{sig.v}); });
  const TermId wrong_sort =
      terms.mk_forall({sig.v}, terms.mk_app(sig.p, {var(0), terms.mk_constant(sig.a)}));
  expect_throw<std::logic_error>([&] { check_bound_variables(terms, wrong_sort); });
}

// ---------------------------------------------------------------------------
// Differential test against a named-variable oracle. Formulas are generated as
// trees with names, converted to de Bruijn terms, and the result of
// `instantiate` is compared with naive substitution on the names after every
// inner binder is renamed apart, which rules out capture.

enum class NamedKind { Variable, ConstantA, ConstantB, F, G, P, And, Not, Forall, Exists };

struct NamedNode {
  NamedKind kind = NamedKind::ConstantA;
  int name = 0;
  std::vector<std::size_t> children;
};

struct NamedTree {
  std::vector<NamedNode> nodes;

  std::size_t add(NamedKind kind, int name = 0) {
    nodes.push_back({kind, name, {}});
    return nodes.size() - 1;
  }
};

std::size_t random_below(std::mt19937& rng, std::size_t bound) {
  return std::uniform_int_distribution<std::size_t>(0, bound - 1)(rng);
}

// Fills the node `root` (already added) with a random formula or U-term.
void generate(NamedTree& tree, std::mt19937& rng, std::size_t root, bool boolean,
              std::vector<int> scope, int depth_limit) {
  struct Hole {
    std::size_t node = 0;
    bool boolean = false;
    int depth = 0;
    std::vector<int> scope;
  };
  std::vector<Hole> holes{{root, boolean, 0, std::move(scope)}};
  while (!holes.empty()) {
    Hole hole = std::move(holes.back());
    holes.pop_back();
    const bool leaf = hole.depth >= depth_limit;
    NamedKind kind = NamedKind::ConstantA;
    if (hole.boolean) {
      static const std::vector<NamedKind> boolean_kinds{
          NamedKind::P, NamedKind::And, NamedKind::Not, NamedKind::Forall, NamedKind::Exists};
      kind = leaf ? NamedKind::P : boolean_kinds[random_below(rng, boolean_kinds.size())];
    } else {
      std::vector<NamedKind> term_kinds{NamedKind::ConstantA, NamedKind::ConstantB};
      if (!hole.scope.empty()) {
        term_kinds.push_back(NamedKind::Variable);
        term_kinds.push_back(NamedKind::Variable);
      }
      if (!leaf) {
        term_kinds.push_back(NamedKind::F);
        term_kinds.push_back(NamedKind::G);
      }
      kind = term_kinds[random_below(rng, term_kinds.size())];
    }
    tree.nodes[hole.node].kind = kind;
    std::vector<std::pair<bool, std::vector<int>>> child_specs;
    switch (kind) {
      case NamedKind::Variable:
        tree.nodes[hole.node].name = hole.scope[random_below(rng, hole.scope.size())];
        break;
      case NamedKind::ConstantA:
      case NamedKind::ConstantB:
        break;
      case NamedKind::F:
        child_specs.push_back({false, hole.scope});
        break;
      case NamedKind::G:
      case NamedKind::P:
        child_specs.push_back({false, hole.scope});
        child_specs.push_back({false, hole.scope});
        break;
      case NamedKind::And:
        child_specs.push_back({true, hole.scope});
        child_specs.push_back({true, hole.scope});
        break;
      case NamedKind::Not:
        child_specs.push_back({true, hole.scope});
        break;
      case NamedKind::Forall:
      case NamedKind::Exists: {
        const int name = static_cast<int>(random_below(rng, 3));
        tree.nodes[hole.node].name = name;
        std::vector<int> inner = hole.scope;
        inner.push_back(name);
        child_specs.push_back({true, std::move(inner)});
        break;
      }
    }
    for (auto& [child_boolean, child_scope] : child_specs) {
      const std::size_t child = tree.add(NamedKind::ConstantA);
      tree.nodes[hole.node].children.push_back(child);
      holes.push_back({child, child_boolean, hole.depth + 1, std::move(child_scope)});
    }
  }
}

TermId convert(TermManager& terms, const Signature& sig, const NamedTree& tree, std::size_t root,
               const std::vector<int>& root_scope) {
  struct Frame {
    std::size_t node = 0;
    std::vector<int> scope;
    bool expanded = false;
  };
  std::vector<TermId> value(tree.nodes.size(), kInvalidTermId);
  std::vector<Frame> stack{{root, root_scope, false}};
  while (!stack.empty()) {
    Frame frame = stack.back();
    const NamedNode& node = tree.nodes[frame.node];
    if (!frame.expanded) {
      stack.back().expanded = true;
      std::vector<int> child_scope = frame.scope;
      if (node.kind == NamedKind::Forall || node.kind == NamedKind::Exists) {
        child_scope.push_back(node.name);
      }
      for (const std::size_t child : node.children) {
        stack.push_back({child, child_scope, false});
      }
      continue;
    }
    stack.pop_back();
    const auto child = [&](std::size_t position) { return value[node.children.at(position)]; };
    TermId result = kInvalidTermId;
    switch (node.kind) {
      case NamedKind::Variable: {
        std::size_t position = frame.scope.size();
        while (position > 0 && frame.scope[position - 1] != node.name) {
          --position;
        }
        assert(position > 0 && "free variable in named oracle");
        result = terms.mk_bound_variable(static_cast<std::uint32_t>(frame.scope.size() - position),
                                         sig.u);
        break;
      }
      case NamedKind::ConstantA:
        result = terms.mk_constant(sig.a);
        break;
      case NamedKind::ConstantB:
        result = terms.mk_constant(sig.b);
        break;
      case NamedKind::F:
        result = terms.mk_app(sig.f, {child(0)});
        break;
      case NamedKind::G:
        result = terms.mk_app(sig.g, {child(0), child(1)});
        break;
      case NamedKind::P:
        result = terms.mk_app(sig.p, {child(0), child(1)});
        break;
      case NamedKind::And:
        result = terms.mk_and({child(0), child(1)});
        break;
      case NamedKind::Not:
        result = terms.mk_not(child(0));
        break;
      case NamedKind::Forall:
        result = terms.mk_forall({sig.u}, child(0));
        break;
      case NamedKind::Exists:
        result = terms.mk_exists({sig.u}, child(0));
        break;
    }
    value[frame.node] = result;
  }
  return value[root];
}

// Copies `root` into `out`, renaming every binder inside it to a fresh name
// and replacing each free occurrence of `target` by a copy of the binder-free
// `replacement` tree. Fresh names start at 1000, above every generated name.
std::size_t substitute_renamed(NamedTree& out, const NamedTree& in, std::size_t root, int target,
                               const NamedTree& replacement, int& next_fresh) {
  struct Frame {
    std::size_t source = 0;
    std::size_t destination = 0;
    std::vector<std::pair<int, int>> renaming;
    bool from_replacement = false;
  };
  const std::size_t result = out.add(NamedKind::ConstantA);
  std::vector<Frame> stack{{root, result, {}, false}};
  while (!stack.empty()) {
    Frame frame = std::move(stack.back());
    stack.pop_back();
    const NamedNode source =
        frame.from_replacement ? replacement.nodes[frame.source] : in.nodes[frame.source];
    NamedNode copy{source.kind, source.name, {}};
    std::vector<std::pair<int, int>> renaming = frame.renaming;
    bool from_replacement = frame.from_replacement;
    std::vector<std::size_t> sources = source.children;
    if (!from_replacement && source.kind == NamedKind::Variable) {
      int renamed = source.name;
      bool bound_inside = false;
      for (auto entry = renaming.rbegin(); entry != renaming.rend(); ++entry) {
        if (entry->first == source.name) {
          renamed = entry->second;
          bound_inside = true;
          break;
        }
      }
      if (!bound_inside && source.name == target) {
        // Continue by copying the replacement's root into this slot.
        stack.push_back({0, frame.destination, {}, true});
        continue;
      }
      copy.name = renamed;
    }
    if (!from_replacement &&
        (source.kind == NamedKind::Forall || source.kind == NamedKind::Exists)) {
      copy.name = next_fresh++;
      renaming.emplace_back(source.name, copy.name);
    }
    out.nodes[frame.destination].kind = copy.kind;
    out.nodes[frame.destination].name = copy.name;
    for (const std::size_t child_source : sources) {
      const std::size_t child = out.add(NamedKind::ConstantA);
      out.nodes[frame.destination].children.push_back(child);
      stack.push_back({child_source, child, renaming, from_replacement});
    }
  }
  return result;
}

void instantiate_agrees_with_a_named_oracle() {
  Signature sig;
  TermManager terms(sig.symbols);
  std::mt19937 rng(20260911U);
  const TermId x0 = terms.mk_bound_variable(0, sig.u);
  for (int round = 0; round < 400; ++round) {
    int next_fresh = 1000;

    // Ground instantiation of a closed forall.
    {
      NamedTree tree;
      const int name = static_cast<int>(random_below(rng, 3));
      const std::size_t root = tree.add(NamedKind::Forall, name);
      const std::size_t body = tree.add(NamedKind::ConstantA);
      tree.nodes[root].children.push_back(body);
      generate(tree, rng, body, true, {name}, 5);

      NamedTree replacement;
      generate(replacement, rng, replacement.add(NamedKind::ConstantA), false, {}, 2);

      const TermId quantifier = convert(terms, sig, tree, root, {});
      const TermId replacement_term = convert(terms, sig, replacement, 0, {});
      NamedTree expected;
      const std::size_t expected_root =
          substitute_renamed(expected, tree, body, name, replacement, next_fresh);
      const TermId actual = instantiate(terms, quantifier, std::vector<TermId>{replacement_term});
      assert(actual == convert(terms, sig, expected, expected_root, {}));
      check_bound_variables(terms, actual);
    }

    // Skolemizing the exists in forall v. exists w. phi with the open term f(v).
    {
      NamedTree tree;
      const int outer = static_cast<int>(random_below(rng, 3));
      const int inner = static_cast<int>(random_below(rng, 3));
      const std::size_t root = tree.add(NamedKind::Forall, outer);
      const std::size_t exists = tree.add(NamedKind::Exists, inner);
      const std::size_t matrix = tree.add(NamedKind::ConstantA);
      tree.nodes[root].children.push_back(exists);
      tree.nodes[exists].children.push_back(matrix);
      generate(tree, rng, matrix, true, {outer, inner}, 5);

      NamedTree skolem;
      const std::size_t skolem_root = skolem.add(NamedKind::F);
      // Add first: add() may reallocate the node vector under a borrowed node.
      const std::size_t skolem_argument = skolem.add(NamedKind::Variable, outer);
      skolem.nodes[skolem_root].children.push_back(skolem_argument);

      const TermId quantifier = convert(terms, sig, tree, root, {});
      const TermId exists_term = quantifier_body(terms, quantifier);
      const TermId actual =
          instantiate(terms, exists_term, std::vector<TermId>{terms.mk_app(sig.f, {x0})});

      NamedTree expected;
      const std::size_t expected_root = expected.add(NamedKind::Forall, outer);
      const std::size_t expected_body =
          substitute_renamed(expected, tree, matrix, inner, skolem, next_fresh);
      expected.nodes[expected_root].children.push_back(expected_body);
      const TermId expected_quantifier = convert(terms, sig, expected, expected_root, {});
      assert(actual == quantifier_body(terms, expected_quantifier));
      check_bound_variables(terms, actual, std::vector<SortId>{sig.u});
    }
  }
}

void deep_binder_nesting_is_iterative() {
  constexpr std::uint32_t depth = 20000;
  Signature sig;
  TermManager terms(sig.symbols);
  const TermId a = terms.mk_constant(sig.a);
  const TermId b = terms.mk_constant(sig.b);
  // The innermost atom refers to the outermost of `depth` binders.
  TermId body = terms.mk_app(sig.p, {terms.mk_bound_variable(depth - 1, sig.u), a});
  const TermId own = terms.mk_app(sig.p, {terms.mk_bound_variable(0, sig.u), b});
  for (std::uint32_t level = 0; level < depth; ++level) {
    body = terms.mk_forall({sig.u}, terms.mk_and({body, own}));
  }
  assert(terms.loose_index(body) == 0);
  assert(terms.loose_index(shift(terms, quantifier_body(terms, body), 1)) == 2);

  TermId cursor = instantiate(terms, body, std::vector<TermId>{b});
  while (terms.get(cursor).kind != TermKind::Application) {
    cursor = terms.get(cursor).kind == TermKind::And ? terms.get(cursor).children.at(0)
                                                     : quantifier_body(terms, cursor);
  }
  assert(cursor == terms.mk_app(sig.p, {b, a}));
}

}  // namespace

int main() {
  constructors_hash_cons_and_validate();
  loose_indices_track_unbound_variables();
  shift_satisfies_its_laws();
  instantiate_follows_the_design_note_examples();
  instantiate_handles_lists_outer_variables_and_errors();
  checker_rejects_ill_scoped_terms();
  instantiate_agrees_with_a_named_oracle();
  deep_binder_nesting_is_iterative();
  return 0;
}
