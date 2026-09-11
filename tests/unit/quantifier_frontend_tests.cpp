#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/binders.hpp"
#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/frontend.hpp"
#include "smtlib/parser.hpp"

using primo::core::SortId;
using primo::core::SymbolId;
using primo::core::TermId;
using primo::core::TermManager;
using primo::smt::CheckSatOptions;
using primo::smt::CheckSatResult;
using primo::smt::Frontend;

namespace {

const std::string kPrelude = R"(
  (set-logic UF)
  (declare-sort U 0)
  (declare-fun a () U)
  (declare-fun f (U) U)
  (declare-fun p (U U) Bool)
  (declare-fun h (Bool) U)
)";

struct Translation {
  std::unique_ptr<Frontend> frontend = std::make_unique<Frontend>();
  std::vector<TermId> assertions;

  TermManager& terms() { return frontend->terms(); }
  SortId u() { return *terms().symbols().lookup_sort("U"); }
  SymbolId symbol(const std::string& name) { return *terms().symbols().lookup_symbol(name); }
  TermId a() { return terms().mk_constant(symbol("a")); }
  TermId var(std::uint32_t index) { return terms().mk_bound_variable(index, u()); }
  TermId p(TermId lhs, TermId rhs) { return terms().mk_app(symbol("p"), {lhs, rhs}); }
  TermId f(TermId argument) { return terms().mk_app(symbol("f"), {argument}); }
  TermId forall(TermId body) { return terms().mk_forall({u()}, body); }
  TermId exists(TermId body) { return terms().mk_exists({u()}, body); }
};

Translation translate(const std::string& commands) {
  const std::string input = kPrelude + commands;
  Translation translation;
  for (const auto& command : primo::smtlib::Parser(input).parse_commands()) {
    translation.frontend->process_command(command, translation.assertions);
  }
  return translation;
}

template <typename Fn>
void expect_rejected(Fn fn) {
  try {
    fn();
  } catch (const std::exception&) {
    return;
  }
  assert(false && "expected the input to be rejected");
}

void variable_lists_and_nesting_use_de_bruijn_indices() {
  {
    Translation t = translate("(assert (forall ((x U) (y U)) (p x y)))");
    assert(t.assertions.size() == 1);
    assert(t.assertions[0] == t.terms().mk_forall({t.u(), t.u()}, t.p(t.var(1), t.var(0))));
    primo::core::check_bound_variables(t.terms(), t.assertions[0]);
  }
  {
    // An inner binder of the same name shadows the outer one.
    Translation t = translate("(assert (forall ((x U)) (exists ((x U)) (p x x))))");
    assert(t.assertions[0] == t.forall(t.exists(t.p(t.var(0), t.var(0)))));
  }
  {
    Translation t = translate("(assert (forall ((x U)) (and (p x a) (exists ((y U)) (p x y)))))");
    assert(t.assertions[0] ==
           t.forall(t.terms().mk_and({t.p(t.var(0), t.a()), t.exists(t.p(t.var(1), t.var(0)))})));
  }
  {
    // SMT-LIB 2.6 section 3.6.1: the last repeated name in a list wins.
    Translation t = translate("(assert (forall ((x U) (x U)) (p x a)))");
    assert(t.assertions[0] == t.terms().mk_forall({t.u(), t.u()}, t.p(t.var(0), t.a())));
  }
  {
    // A bound variable shadows a declared constant of the same name.
    Translation t = translate("(declare-fun x () U) (assert (forall ((x U)) (p x x)))");
    assert(t.assertions[0] == t.forall(t.p(t.var(0), t.var(0))));
  }
  {
    // Quoted and unquoted spellings name the same variable.
    Translation t = translate("(assert (forall ((|x| U)) (p x |x|)))");
    assert(t.assertions[0] == t.forall(t.p(t.var(0), t.var(0))));
  }
}

void let_values_are_shifted_where_they_are_used() {
  {
    // Built at depth 1, used at depth 2: x is %1 inside the exists.
    Translation t =
        translate("(assert (forall ((x U)) (let ((t (f x))) (exists ((y U)) (p t y)))))");
    assert(t.assertions[0] == t.forall(t.exists(t.p(t.f(t.var(1)), t.var(0)))));
  }
  {
    // A closed value is unchanged under binders.
    Translation t = translate("(assert (let ((t (f a))) (forall ((x U)) (p t x))))");
    assert(t.assertions[0] == t.forall(t.p(t.f(t.a()), t.var(0))));
  }
  {
    // The inner binder reuses the name the let value mentions: no capture.
    Translation t = translate("(assert (forall ((x U)) (let ((t x)) (forall ((x U)) (p t x)))))");
    assert(t.assertions[0] == t.forall(t.forall(t.p(t.var(1), t.var(0)))));
  }
}

void definitions_are_expanded_under_binders() {
  {
    Translation t = translate(
        "(define-fun q ((z U)) Bool (exists ((x U)) (p z x))) (assert (forall ((x U)) (q x)))");
    assert(t.assertions[0] == t.forall(t.exists(t.p(t.var(1), t.var(0)))));
  }
  {
    Translation t = translate(
        "(define-fun ax () Bool (forall ((x U)) (p x x)))"
        "(assert (forall ((y U)) (and ax (p y a))))");
    const TermId ax = t.forall(t.p(t.var(0), t.var(0)));
    assert(t.assertions[0] == t.forall(t.terms().mk_and({ax, t.p(t.var(0), t.a())})));
  }
}

void patterns_are_kept_and_other_attributes_dropped() {
  Translation t = translate(
      "(assert (forall ((x U)) (! (p x a) :pattern ((f x) (p x a)) :qid ax1 :pattern ((f x)))))");
  const TermId both = t.terms().mk_pattern({t.f(t.var(0)), t.p(t.var(0), t.a())});
  const TermId single = t.terms().mk_pattern({t.f(t.var(0))});
  assert(t.assertions[0] == t.terms().mk_forall({t.u()}, t.p(t.var(0), t.a()), {both, single}));
  assert(primo::core::quantifier_patterns(t.terms(), t.assertions[0]).size() == 2);
}

void open_boolean_arguments_are_not_named() {
  {
    Translation t = translate("(assert (forall ((x U)) (= (h (p x a)) a)))");
    assert(t.assertions.size() == 1);
    const TermId application = t.terms().mk_app(t.symbol("h"), {t.p(t.var(0), t.a())});
    assert(t.assertions[0] == t.forall(t.terms().mk_eq(application, t.a())));
  }
  {
    // A closed argument is still named, with its side assertion.
    Translation t = translate("(assert (= (h (p a a)) a))");
    assert(t.assertions.size() == 2);
  }
}

void malformed_quantifiers_are_rejected() {
  expect_rejected([] { static_cast<void>(translate("(assert (forall ((x U)) x))")); });
  expect_rejected([] { static_cast<void>(translate("(assert (forall () (p a a)))")); });
  expect_rejected([] { static_cast<void>(translate("(assert (forall ((x V)) (p a a)))")); });
  expect_rejected([] { static_cast<void>(translate("(assert (forall (x U) (p a a)))")); });
  expect_rejected(
      [] { static_cast<void>(translate("(assert (forall ((x U)) (! (p x x) :pattern)))")); });
}

void quantified_assertions_are_gated_before_solving() {
  const auto quantified =
      primo::smt::check_sat_smtlib(kPrelude + "(assert (forall ((x U)) (p x x))) (check-sat)");
  assert(quantified.result == CheckSatResult::Unsupported);
  assert(quantified.message.find("quantified") != std::string::npos);

  // Logic UF without quantifiers solves on the ground route.
  assert(primo::smt::check_sat_smtlib(kPrelude + "(assert (p a a)) (check-sat)").result ==
         CheckSatResult::Sat);
  // An unused quantified definition does not trigger the gate.
  assert(primo::smt::check_sat_smtlib(
             kPrelude + "(define-fun ax () Bool (forall ((x U)) (p x x))) (assert (p a a)) "
                        "(check-sat)")
             .result == CheckSatResult::Sat);

  CheckSatOptions assumptions;
  assumptions.incremental_mode = primo::smt::IncrementalMode::Assumptions;
  const auto scoped = primo::smt::check_sat_smtlib(
      "(set-logic QF_UF) (declare-sort U 0) (declare-fun p (U) Bool) "
      "(assert (forall ((x U)) (p x))) (check-sat)",
      assumptions);
  assert(scoped.result == CheckSatResult::Unsupported);
}

}  // namespace

int main() {
  variable_lists_and_nesting_use_de_bruijn_indices();
  let_values_are_shifted_where_they_are_used();
  definitions_are_expanded_under_binders();
  patterns_are_kept_and_other_attributes_dropped();
  open_boolean_arguments_are_not_named();
  malformed_quantifiers_are_rejected();
  quantified_assertions_are_gated_before_solving();
  return 0;
}
