#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <string>
#include <variant>
#include <vector>

#include "core/ids.hpp"
#include "core/model.hpp"
#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smtlib/printer.hpp"
#include "util/rational.hpp"

using namespace primo;

namespace {
template <typename Fn>
void rejects(Fn fn) {
  bool rejected = false;
  try {
    fn();
  } catch (const core::ModelError&) {
    rejected = true;
  }
  assert(rejected);
}

void typed_tables_and_validation() {
  core::SymbolTable symbols;
  const auto i = symbols.int_sort();
  const auto r = symbols.real_sort();
  const auto f = symbols.declare_function("f", {i, symbols.bool_sort()}, i);
  const core::ModelValue zero{i, util::Rational(0)}, one{i, util::Rational(1)};
  const core::ModelValue yes{symbols.bool_sort(), true};
  const core::Model model(symbols, {{f, zero, {{{one, yes}, one}}}});
  assert(model.apply(f, std::vector{one, yes}) == one);
  assert(model.apply(f, std::vector{zero, yes}) == zero);
  rejects([&] { model.apply(f, std::vector{one}); });
  rejects([&] { model.apply(f, std::vector{core::ModelValue{r, util::Rational(1)}, yes}); });
  rejects([&] { core::Model bad(symbols, {{f, zero, {{{one, yes}, one}, {{one, yes}, zero}}}}); });
  rejects([&] { core::Model bad(symbols, {{f, {i, util::Rational(1, 2)}, {}}}); });
  rejects([&] { core::Model bad(symbols, {{f, {i, true}, {}}}); });
  const auto a = symbols.declare_sort("A");
  const auto b = symbols.declare_sort("B");
  assert((core::ModelValue{a, core::DomainValue{0}} != core::ModelValue{b, core::DomainValue{0}}));
  assert((core::ModelValue{i, util::Rational(1)} != core::ModelValue{r, util::Rational(1)}));
}

void interpreted_terms_use_the_frozen_table() {
  core::SymbolTable symbols;
  const auto i = symbols.int_sort();
  const auto x = symbols.declare_constant("x", i);
  const auto f = symbols.declare_function("f", {i}, i);
  const auto p = symbols.declare_constant("p", symbols.bool_sort());
  // Missing internal interpretation is intentional: the unselected branch
  // must not be evaluated or consult an unrelated term-value cache.
  const auto absent = symbols.declare_constant("$primo_absent", i);
  core::TermManager terms(symbols);
  const auto n = [&](int value) { return terms.mk_rational(util::Rational(value), i); };
  const core::Model model(
      symbols, {{x, {i, util::Rational(2)}, {}},
                {f, {i, util::Rational(0)}, {{{{i, util::Rational(3)}}, {i, util::Rational(7)}}}},
                {p, {symbols.bool_sort(), true}, {}}});
  core::ModelEvaluator evaluator(terms, model);
  const auto app = terms.mk_app(f, {terms.mk_plus({terms.mk_constant(x), n(1)})});
  const auto y = terms.mk_plus({app, n(1)});
  assert(evaluator.evaluate(y) == (core::ModelValue{i, util::Rational(8)}));
  const auto condition = terms.mk_and({terms.mk_constant(p), terms.mk_lt(n(1), app)});
  const auto selected = terms.mk_ite(condition, y, terms.mk_constant(absent));
  assert(evaluator.evaluate(selected) == evaluator.evaluate(y));
  const auto checked = terms.mk_eq(selected, n(8));
  assert(std::get<bool>(evaluator.evaluate(checked).payload));
  const auto before = evaluator.evaluated_terms();
  evaluator.evaluate(checked);
  assert(evaluator.evaluated_terms() == before);
  rejects([&] { evaluator.evaluate(terms.mk_constant(absent)); });
}

void exact_integer_and_real_semantics() {
  core::SymbolTable symbols;
  const auto x = symbols.declare_constant("x", symbols.int_sort());
  const auto y = symbols.declare_constant("y", symbols.real_sort());
  core::TermManager terms(symbols);
  const auto term = terms.mk_constant(x);
  const auto third = terms.mk_constant(y);
  for (int value = -50; value <= 50; ++value) {
    const core::Model model(symbols, {{x, {symbols.int_sort(), util::Rational(value)}, {}},
                                      {y, {symbols.real_sort(), util::Rational(1, 3)}, {}}});
    core::ModelEvaluator evaluator(terms, model);
    for (int divisor = 1; divisor <= 5; ++divisor) {
      const auto quotient = terms.mk_int_div(util::Rational(divisor), term);
      const auto remainder = terms.mk_int_mod(util::Rational(divisor), term);
      const int expected = value >= 0 ? value / divisor : -((-value + divisor - 1) / divisor);
      assert(std::get<util::Rational>(evaluator.evaluate(quotient).payload) ==
             util::Rational(expected));
      assert(std::get<util::Rational>(evaluator.evaluate(remainder).payload) ==
             util::Rational(value - divisor * expected));
      assert(std::get<util::Rational>(evaluator.evaluate(terms.mk_abs(term)).payload) ==
             util::Rational(value < 0 ? -value : value));
    }
    assert(std::get<util::Rational>(
               evaluator.evaluate(terms.mk_times(util::Rational(-3), third)).payload) ==
           util::Rational(-1));
  }
}

void boolean_connectives() {
  core::SymbolTable symbols;
  const auto p = symbols.declare_constant("p", symbols.bool_sort());
  const auto q = symbols.declare_constant("q", symbols.bool_sort());
  core::TermManager terms(symbols);
  const auto a = terms.mk_constant(p), b = terms.mk_constant(q);
  for (const bool av : {false, true}) {
    for (const bool bv : {false, true}) {
      const core::Model model(
          symbols, {{p, {symbols.bool_sort(), av}, {}}, {q, {symbols.bool_sort(), bv}, {}}});
      core::ModelEvaluator evaluator(terms, model);
      const auto evaluate = [&](core::TermId term) {
        return std::get<bool>(evaluator.evaluate(term).payload);
      };
      assert(evaluate(terms.mk_not(a)) == !av);
      assert(evaluate(terms.mk_and({a, b})) == (av && bv));
      assert(evaluate(terms.mk_or({a, b})) == (av || bv));
      assert(evaluate(terms.mk_implies(a, b)) == (!av || bv));
      assert(evaluate(terms.mk_eq(a, b)) == (av == bv));
    }
  }
}

void deep_applications_are_iterative() {
  core::SymbolTable symbols;
  const auto i = symbols.int_sort();
  const auto f = symbols.declare_function("f", {i}, i);
  core::TermManager terms(symbols);
  const core::Model model(symbols, {{f, {i, util::Rational(0)}, {}}});
  auto term = terms.mk_rational(util::Rational(3), i);
  for (int depth = 0; depth < 30000; ++depth) term = terms.mk_app(f, {term});
  core::ModelEvaluator evaluator(terms, model);
  assert(evaluator.evaluate(term) == (core::ModelValue{i, util::Rational(0)}));
  assert(evaluator.evaluated_terms() == 30001);
}

void printer_owns_signature_and_preserves_meaning() {
  const core::Model model = [] {
    core::SymbolTable symbols;
    const auto i = symbols.int_sort();
    const auto x = symbols.declare_constant("x0", i);
    const auto y = symbols.declare_constant("a b", i);
    const auto f = symbols.declare_function("f", {i}, i);
    return core::Model(
        symbols,
        {{x, {i, util::Rational(2)}, {}},
         {y, {i, util::Rational(8)}, {}},
         {f, {i, util::Rational(0)}, {{{{i, util::Rational(3)}}, {i, util::Rational(7)}}}}});
  }();
  const std::string printed = smtlib::print_model(model);
  assert(printed == smtlib::print_model(model));
  assert(printed.find("|a b|") != std::string::npos);
  assert(printed.find("@primo_model_arg_0") != std::string::npos);
  const auto checked =
      smt::check_sat_smtlib("(set-logic QF_UFLIA)\n" + printed.substr(6, printed.size() - 7) +
                            "\n(assert (not (= |a b| (+ (f (+ x0 1)) 1))))\n(check-sat)\n");
  assert(checked.result == smt::CheckSatResult::Unsat);
}
}  // namespace

int main() {
  typed_tables_and_validation();
  interpreted_terms_use_the_frozen_table();
  exact_integer_and_real_semantics();
  boolean_connectives();
  deep_applications_are_iterative();
  printer_owns_signature_and_preserves_meaning();
}
