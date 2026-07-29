#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <stdexcept>
#include <vector>

#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "util/rational.hpp"

using primo::core::SortKind;
using primo::core::SymbolKind;
using primo::core::SymbolTable;
using primo::core::TermKind;
using primo::core::TermManager;
using primo::util::Rational;

namespace {

template <typename Fn>
void expect_invalid_argument(Fn fn) {
  try {
    fn();
  } catch (const std::invalid_argument&) {
    return;
  }
  assert(false && "expected std::invalid_argument");
}

void symbol_table_tracks_sorts_and_symbols() {
  SymbolTable symbols;
  const auto bool_sort = symbols.bool_sort();
  assert(symbols.get_sort(bool_sort).name == "Bool");
  assert(symbols.get_sort(bool_sort).kind == SortKind::Bool);

  const auto real_sort = symbols.real_sort();
  assert(symbols.get_sort(real_sort).name == "Real");
  assert(symbols.get_sort(real_sort).kind == SortKind::Real);
  assert(symbols.lookup_sort("Real") == real_sort);

  const auto sort_a = symbols.declare_sort("A");
  assert(symbols.lookup_sort("A") == sort_a);
  assert(symbols.get_sort(sort_a).kind == SortKind::Uninterpreted);

  const auto a = symbols.declare_constant("a", sort_a);
  const auto f = symbols.declare_function("f", {sort_a}, sort_a);
  const auto p = symbols.declare_predicate("p", {sort_a});

  assert(symbols.lookup_symbol("a") == a);
  assert(symbols.get_symbol(a).kind == SymbolKind::Constant);
  assert(symbols.get_symbol(f).arg_sorts == std::vector{sort_a});
  assert(symbols.get_symbol(p).result_sort == bool_sort);

  expect_invalid_argument([&] { symbols.declare_sort("A"); });
  expect_invalid_argument([&] { symbols.declare_constant("a", sort_a); });
}

void term_manager_hash_conses_and_checks_sorts() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto sort_b = symbols.declare_sort("B");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto b_symbol = symbols.declare_constant("b", sort_b);
  const auto f_symbol = symbols.declare_function("f", {sort_a}, sort_a);
  const auto pred_symbol = symbols.declare_predicate("p", {sort_a});

  TermManager terms(symbols);
  const auto a0 = terms.mk_constant(a_symbol);
  const auto a1 = terms.mk_constant(a_symbol);
  const auto b = terms.mk_constant(b_symbol);
  assert(a0 == a1);
  assert(terms.get(a0).sort == sort_a);
  assert(terms.size() == 2);

  const auto f_a0 = terms.mk_app(f_symbol, {a0});
  const auto f_a1 = terms.mk_app(f_symbol, {a1});
  assert(f_a0 == f_a1);
  assert(terms.get(f_a0).kind == TermKind::Application);

  const auto p_a = terms.mk_app(pred_symbol, {a0});
  assert(terms.get(p_a).sort == symbols.bool_sort());

  const auto eq0 = terms.mk_eq(a0, f_a0);
  const auto eq1 = terms.mk_eq(a1, f_a1);
  assert(eq0 == eq1);
  assert(terms.get(eq0).kind == TermKind::Equality);
  assert(terms.get(eq0).sort == symbols.bool_sort());

  const auto negated_eq = terms.mk_not(eq0);
  assert(terms.get(negated_eq).children == std::vector{eq0});

  const auto conjunction = terms.mk_and({eq0, negated_eq, p_a});
  assert(terms.get(conjunction).children.size() == 3);

  expect_invalid_argument([&] { terms.mk_eq(a0, b); });
  expect_invalid_argument([&] { terms.mk_app(f_symbol, {b}); });
  expect_invalid_argument([&] { terms.mk_not(a0); });
  expect_invalid_argument([&] { terms.mk_app(a_symbol, {}); });
}

void Boolean_terms_are_hash_consed() {
  SymbolTable symbols;
  TermManager terms(symbols);

  const auto t0 = terms.mk_bool(true);
  const auto t1 = terms.mk_bool(true);
  const auto f0 = terms.mk_bool(false);
  assert(t0 == t1);
  assert(t0 != f0);
  assert(terms.get(t0).bool_value);
  assert(!terms.get(f0).bool_value);
}

void trivial_boolean_terms_are_simplified() {
  SymbolTable symbols;
  const auto sort_a = symbols.declare_sort("A");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  const auto p_symbol = symbols.declare_constant("p", symbols.bool_sort());
  const auto q_symbol = symbols.declare_constant("q", symbols.bool_sort());
  TermManager terms(symbols);

  const auto a = terms.mk_constant(a_symbol);
  const auto p = terms.mk_constant(p_symbol);
  const auto q = terms.mk_constant(q_symbol);
  const auto t = terms.mk_bool(true);
  const auto f = terms.mk_bool(false);

  assert(terms.mk_eq(a, a) == t);
  assert(terms.mk_not(t) == f);
  assert(terms.mk_not(terms.mk_not(p)) == p);
  assert(terms.mk_and({t, p}) == p);
  assert(terms.mk_and({p, f, q}) == f);
  assert(terms.mk_or({f, p}) == p);
  assert(terms.mk_or({p, t, q}) == t);
  assert(terms.mk_implies(t, p) == p);
  assert(terms.mk_implies(f, p) == t);
  assert(terms.mk_implies(p, f) == terms.mk_not(p));
  assert(terms.mk_implies(p, p) == t);
}

void arithmetic_terms_are_hash_consed_and_checked() {
  SymbolTable symbols;
  const auto x_symbol = symbols.declare_constant("x", symbols.real_sort());
  const auto y_symbol = symbols.declare_constant("y", symbols.real_sort());
  const auto sort_a = symbols.declare_sort("A");
  const auto a_symbol = symbols.declare_constant("a", sort_a);
  TermManager terms(symbols);

  const auto x = terms.mk_constant(x_symbol);
  const auto y = terms.mk_constant(y_symbol);
  const auto a = terms.mk_constant(a_symbol);

  const auto three0 = terms.mk_rational(Rational(3));
  const auto three1 = terms.mk_rational(Rational(3));
  assert(three0 == three1);
  assert(terms.get(three0).kind == TermKind::Rational);
  assert(terms.get(three0).sort == symbols.real_sort());
  assert(terms.get(three0).rational_value == Rational(3));

  const auto sum0 = terms.mk_plus({x, y, three0});
  const auto sum1 = terms.mk_plus({x, y, three1});
  assert(sum0 == sum1);
  assert(terms.get(sum0).kind == TermKind::Plus);
  assert(terms.get(sum0).sort == symbols.real_sort());

  const auto scaled_x0 = terms.mk_times(Rational(2), x);
  const auto scaled_x1 = terms.mk_times(Rational(2), x);
  assert(scaled_x0 == scaled_x1);
  assert(terms.get(scaled_x0).kind == TermKind::Times);
  assert(terms.get(scaled_x0).rational_value == Rational(2));
  assert(terms.get(scaled_x0).children == std::vector{x});

  const auto le0 = terms.mk_le(x, y);
  const auto le1 = terms.mk_le(x, y);
  assert(le0 == le1);
  assert(terms.get(le0).kind == TermKind::Le);
  assert(terms.get(le0).sort == symbols.bool_sort());

  const auto lt0 = terms.mk_lt(x, y);
  assert(terms.get(lt0).kind == TermKind::Lt);
  assert(terms.get(lt0).sort == symbols.bool_sort());
  assert(lt0 != le0);

  expect_invalid_argument([&] { terms.mk_plus({x, a}); });
  expect_invalid_argument([&] { terms.mk_times(Rational(2), a); });
  expect_invalid_argument([&] { terms.mk_le(x, a); });
  expect_invalid_argument([&] { terms.mk_plus({}); });
}

void arithmetic_terms_are_simplified() {
  SymbolTable symbols;
  const auto x_symbol = symbols.declare_constant("x", symbols.real_sort());
  TermManager terms(symbols);
  const auto x = terms.mk_constant(x_symbol);

  // Constant folding.
  assert(terms.mk_plus({terms.mk_rational(Rational(2)), terms.mk_rational(Rational(3))}) ==
         terms.mk_rational(Rational(5)));
  assert(terms.mk_times(Rational(-1), terms.mk_rational(Rational(3))) ==
         terms.mk_rational(Rational(-3)));
  assert(terms.mk_le(terms.mk_rational(Rational(2)), terms.mk_rational(Rational(3))) ==
         terms.mk_bool(true));
  assert(terms.mk_lt(terms.mk_rational(Rational(3)), terms.mk_rational(Rational(2))) ==
         terms.mk_bool(false));

  // Coefficient folding: nested Times multiplies coefficients; 0 elides to
  // the zero constant; 1 is the identity; nested Rational folds directly.
  assert(terms.mk_times(Rational(3), terms.mk_times(Rational(2), x)) ==
         terms.mk_times(Rational(6), x));
  assert(terms.mk_times(Rational(0), x) == terms.mk_rational(Rational(0)));
  assert(terms.mk_times(Rational(1), x) == x);
  assert(terms.mk_plus({x, terms.mk_rational(Rational(0))}) == x);

  // Nested Plus flattens.
  const auto y_symbol = symbols.declare_constant("y", symbols.real_sort());
  const auto y = terms.mk_constant(y_symbol);
  const auto inner = terms.mk_plus({x, y});
  const auto outer = terms.mk_plus({inner, terms.mk_rational(Rational(1))});
  assert(terms.get(outer).children.size() == 3);

  // Same-term Le/Lt fold like mk_eq's lhs==rhs shortcut.
  assert(terms.mk_le(x, x) == terms.mk_bool(true));
  assert(terms.mk_lt(x, x) == terms.mk_bool(false));
}

}  // namespace

int main() {
  symbol_table_tracks_sorts_and_symbols();
  term_manager_hash_conses_and_checks_sorts();
  Boolean_terms_are_hash_consed();
  trivial_boolean_terms_are_simplified();
  arithmetic_terms_are_hash_consed_and_checked();
  arithmetic_terms_are_simplified();
  return 0;
}
