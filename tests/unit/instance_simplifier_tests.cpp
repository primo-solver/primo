#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <random>
#include <span>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "core/model.hpp"
#include "core/symbol_table.hpp"
#include "core/term.hpp"
#include "core/term_manager.hpp"
#include "theory/quantifiers/instance_simplifier.hpp"

using primo::core::DomainValue;
using primo::core::ModelEvaluator;
using primo::core::ModelValue;
using primo::core::SortId;
using primo::core::SymbolId;
using primo::core::SymbolTable;
using primo::core::Term;
using primo::core::TermId;
using primo::core::TermKind;
using primo::core::TermManager;
using primo::theory::InstanceSimplifier;

namespace {

constexpr std::size_t kDomain = 3;

struct Signature {
  SymbolTable symbols;
  SortId u = 0;
  SymbolId a = 0;
  SymbolId b = 0;
  SymbolId c = 0;
  SymbolId f = 0;  // U -> U
  SymbolId p = 0;  // U -> Bool
  SymbolId q = 0;  // U U -> Bool
  SymbolId x = 0;  // Bool
  SymbolId y = 0;
  SymbolId z = 0;

  Signature() {
    u = symbols.declare_sort("U");
    a = symbols.declare_constant("a", u);
    b = symbols.declare_constant("b", u);
    c = symbols.declare_constant("c", u);
    f = symbols.declare_function("f", {u}, u);
    p = symbols.declare_predicate("p", {u});
    q = symbols.declare_predicate("q", {u, u});
    x = symbols.declare_constant("x", symbols.bool_sort());
    y = symbols.declare_constant("y", symbols.bool_sort());
    z = symbols.declare_constant("z", symbols.bool_sort());
  }
};

// Canonical shape: no implication, connectives flat and strictly sorted,
// equalities oriented, no double negation, and no constant below the root.
void check_canonical(const TermManager& terms, TermId root) {
  std::vector<TermId> pending{root};
  std::unordered_set<TermId> seen;
  while (!pending.empty()) {
    const TermId current = pending.back();
    pending.pop_back();
    if (!seen.insert(current).second) {
      continue;
    }
    const Term& node = terms.get(current);
    if (node.kind == TermKind::Forall || node.kind == TermKind::Exists) {
      continue;
    }
    assert(node.kind != TermKind::Implies);
    if (node.kind == TermKind::And || node.kind == TermKind::Or) {
      assert(node.children.size() >= 2);
      for (std::size_t i = 0; i < node.children.size(); ++i) {
        const Term& child = terms.get(node.children[i]);
        assert(child.kind != node.kind);
        assert(child.kind != TermKind::BoolValue);
        assert(i == 0 || node.children[i - 1] < node.children[i]);
      }
    }
    if (node.kind == TermKind::Equality) {
      assert(node.children[0] < node.children[1]);
    }
    if (node.kind == TermKind::Not) {
      assert(terms.get(node.children[0]).kind != TermKind::Not);
    }
    assert(current == root || node.kind != TermKind::BoolValue);
    pending.insert(pending.end(), node.children.begin(), node.children.end());
  }
}

void rules_normalize_trivia() {
  Signature sig;
  TermManager terms(sig.symbols);
  InstanceSimplifier simplifier(terms);
  const TermId a = terms.mk_constant(sig.a);
  const TermId b = terms.mk_constant(sig.b);
  const TermId x = terms.mk_constant(sig.x);
  const TermId y = terms.mk_constant(sig.y);
  const TermId z = terms.mk_constant(sig.z);
  const TermId yes = terms.mk_bool(true);
  const TermId no = terms.mk_bool(false);
  const auto s = [&](TermId t) { return simplifier.simplify(t); };

  // Implication and operand order.
  assert(s(terms.mk_implies(x, y)) == s(terms.mk_or({terms.mk_not(x), y})));
  assert(s(terms.mk_or({x, y})) == s(terms.mk_or({y, x})));
  assert(s(terms.mk_and({x, terms.mk_and({y, z})})) == s(terms.mk_and({z, terms.mk_and({x, y})})));
  // Complementary operands, including across nesting.
  assert(s(terms.mk_or({x, terms.mk_not(x)})) == yes);
  assert(s(terms.mk_and({terms.mk_and({x, y}), terms.mk_not(x)})) == no);
  assert(s(terms.mk_or({x, terms.mk_or({y, terms.mk_not(x)})})) == yes);
  assert(s(terms.mk_implies(x, x)) == yes);
  // Equalities.
  assert(s(terms.mk_eq(a, b)) == s(terms.mk_eq(b, a)));
  assert(s(terms.mk_eq(x, yes)) == x);
  assert(s(terms.mk_eq(no, x)) == terms.mk_not(x));
  assert(s(terms.mk_eq(x, terms.mk_not(x))) == no);
  // If-then-else.
  assert(s(terms.mk_ite(x, a, a)) == a);
  assert(s(terms.mk_ite(terms.mk_not(x), a, b)) == s(terms.mk_ite(x, b, a)));
  assert(s(terms.mk_ite(x, yes, no)) == x);
  assert(s(terms.mk_ite(x, no, yes)) == terms.mk_not(x));
  assert(s(terms.mk_ite(x, yes, y)) == s(terms.mk_or({x, y})));
  assert(s(terms.mk_ite(x, no, y)) == s(terms.mk_and({terms.mk_not(x), y})));
  assert(s(terms.mk_ite(x, y, yes)) == s(terms.mk_or({terms.mk_not(x), y})));
  assert(s(terms.mk_ite(x, y, no)) == s(terms.mk_and({x, y})));
  // Inside an uninterpreted application.
  const TermId pa = terms.mk_app(sig.p, {terms.mk_ite(x, a, a)});
  assert(s(pa) == terms.mk_app(sig.p, {a}));
  // Already canonical terms are returned unchanged.
  const TermId clause = s(terms.mk_or({y, x}));
  assert(s(clause) == clause);
  check_canonical(terms, clause);
}

void quantifiers_are_opaque() {
  Signature sig;
  TermManager terms(sig.symbols);
  InstanceSimplifier simplifier(terms);
  const TermId v0 = terms.mk_bound_variable(0, sig.u);
  const TermId pv = terms.mk_app(sig.p, {v0});
  // The body is deliberately non-canonical and must stay so.
  const TermId body = terms.mk_or({pv, terms.mk_implies(pv, pv)});
  const TermId forall = terms.mk_forall({sig.u}, body);
  assert(simplifier.simplify(forall) == forall);
  // A quantified atom is still an operand like any other.
  assert(simplifier.simplify(terms.mk_or({forall, terms.mk_not(forall)})) == terms.mk_bool(true));
  const TermId x = terms.mk_constant(sig.x);
  const TermId mixed = simplifier.simplify(terms.mk_implies(x, forall));
  assert(mixed == simplifier.simplify(terms.mk_or({forall, terms.mk_not(x)})));
}

// Random formulas built bottom-up from pools, so no generator recursion.
std::vector<TermId> random_formulas(TermManager& terms, const Signature& sig, std::mt19937& rng,
                                    std::size_t steps) {
  std::vector<TermId> us{terms.mk_constant(sig.a), terms.mk_constant(sig.b),
                         terms.mk_constant(sig.c)};
  std::vector<TermId> bools{terms.mk_constant(sig.x), terms.mk_constant(sig.y),
                            terms.mk_constant(sig.z), terms.mk_bool(true), terms.mk_bool(false)};
  const auto pick = [&](const std::vector<TermId>& from) {
    return from[std::uniform_int_distribution<std::size_t>(0, from.size() - 1)(rng)];
  };
  std::vector<TermId> formulas;
  for (std::size_t step = 0; step < steps; ++step) {
    const int op = std::uniform_int_distribution<int>(0, 11)(rng);
    switch (op) {
      case 0:
        us.push_back(terms.mk_app(sig.f, {pick(us)}));
        break;
      case 1:
        us.push_back(terms.mk_ite(pick(bools), pick(us), pick(us)));
        break;
      case 2:
        bools.push_back(terms.mk_app(sig.p, {pick(us)}));
        break;
      case 3:
        bools.push_back(terms.mk_app(sig.q, {pick(us), pick(us)}));
        break;
      case 4:
        bools.push_back(terms.mk_eq(pick(us), pick(us)));
        break;
      case 5:
        bools.push_back(terms.mk_eq(pick(bools), pick(bools)));
        break;
      case 6:
        bools.push_back(terms.mk_not(pick(bools)));
        break;
      case 7:
      case 8: {
        std::vector<TermId> operands;
        const std::size_t width = std::uniform_int_distribution<std::size_t>(2, 4)(rng);
        for (std::size_t i = 0; i < width; ++i) {
          const TermId operand = pick(bools);
          // Complementary pairs are what make the rules fire; seed them.
          operands.push_back(
              std::uniform_int_distribution<int>(0, 5)(rng) == 0 ? terms.mk_not(operand) : operand);
          if (i == 0 && std::uniform_int_distribution<int>(0, 3)(rng) == 0) {
            operands.push_back(terms.mk_not(operand));
          }
        }
        bools.push_back(op == 7 ? terms.mk_and(std::move(operands))
                                : terms.mk_or(std::move(operands)));
        break;
      }
      case 9:
        bools.push_back(terms.mk_implies(pick(bools), pick(bools)));
        break;
      case 10:
        bools.push_back(terms.mk_ite(pick(bools), pick(bools), pick(bools)));
        break;
      default:
        formulas.push_back(pick(bools));
        break;
    }
  }
  formulas.insert(
      formulas.end(),
      bools.end() - static_cast<std::ptrdiff_t>(std::min<std::size_t>(bools.size(), 40)),
      bools.end());
  return formulas;
}

void simplification_preserves_semantics() {
  Signature sig;
  TermManager terms(sig.symbols);
  const SortId boolean = sig.symbols.bool_sort();
  std::mt19937 rng(20260912);
  std::size_t formulas_checked = 0;
  std::size_t changed = 0;
  std::size_t constants = 0;
  for (std::size_t round = 0; round < 40; ++round) {
    InstanceSimplifier simplifier(terms);
    for (const TermId formula : random_formulas(terms, sig, rng, 120)) {
      const TermId simplified = simplifier.simplify(formula);
      ++formulas_checked;
      changed += simplified != formula;
      constants += terms.get(simplified).kind == TermKind::BoolValue;
      check_canonical(terms, simplified);
      // A fixed point, also for a simplifier that has not seen the input.
      InstanceSimplifier fresh(terms);
      assert(fresh.simplify(simplified) == simplified);
      for (std::size_t sample = 0; sample < 48; ++sample) {
        std::vector<std::size_t> constant_values(3);
        for (auto& value : constant_values) {
          value = std::uniform_int_distribution<std::size_t>(0, kDomain - 1)(rng);
        }
        std::vector<std::size_t> f_table(kDomain);
        for (auto& value : f_table) {
          value = std::uniform_int_distribution<std::size_t>(0, kDomain - 1)(rng);
        }
        std::vector<bool> p_table(kDomain);
        std::vector<bool> q_table(kDomain * kDomain);
        std::vector<bool> bool_values(3);
        for (std::size_t i = 0; i < p_table.size(); ++i) p_table[i] = rng() % 2;
        for (std::size_t i = 0; i < q_table.size(); ++i) q_table[i] = rng() % 2;
        for (std::size_t i = 0; i < bool_values.size(); ++i) bool_values[i] = rng() % 2;
        const auto resolver = [&](const Term& node, std::span<const ModelValue> args) {
          const auto domain = [&](std::size_t index) {
            return ModelValue{sig.u, DomainValue{index}};
          };
          const auto index_of = [](const ModelValue& value) {
            return std::get<DomainValue>(value.payload).index;
          };
          if (node.symbol == sig.a) return domain(constant_values[0]);
          if (node.symbol == sig.b) return domain(constant_values[1]);
          if (node.symbol == sig.c) return domain(constant_values[2]);
          if (node.symbol == sig.x) return ModelValue{boolean, bool(bool_values[0])};
          if (node.symbol == sig.y) return ModelValue{boolean, bool(bool_values[1])};
          if (node.symbol == sig.z) return ModelValue{boolean, bool(bool_values[2])};
          if (node.symbol == sig.f) return domain(f_table[index_of(args[0])]);
          if (node.symbol == sig.p) return ModelValue{boolean, bool(p_table[index_of(args[0])])};
          assert(node.symbol == sig.q);
          return ModelValue{boolean,
                            bool(q_table[index_of(args[0]) * kDomain + index_of(args[1])])};
        };
        ModelEvaluator evaluator(terms, resolver);
        assert(evaluator.evaluate(formula) == evaluator.evaluate(simplified));
      }
    }
  }
  // The check is only meaningful if the rules actually fired.
  assert(formulas_checked > 1000);
  assert(changed * 4 > formulas_checked);
  assert(constants > 0);
}

}  // namespace

int main() {
  rules_normalize_trivia();
  quantifiers_are_opaque();
  simplification_preserves_semantics();
  return 0;
}
