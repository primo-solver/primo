#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/symbol_table.hpp"
#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/frontend.hpp"
#include "smtlib/parser.hpp"
#include "smtlib/term_printer.hpp"
#include "util/rational.hpp"

using primo::core::TermId;
using primo::smt::Frontend;
using primo::smtlib::print_term;

namespace {

const std::string kPrelude = R"(
  (set-logic UF)
  (declare-sort U 0)
  (declare-sort |odd sort| 0)
  (declare-fun a () U)
  (declare-fun |b c| () U)
  (declare-fun f (U) U)
  (declare-fun p (U U) Bool)
  (declare-fun q (|odd sort|) Bool)
  (declare-fun ?x0 () U)
  (declare-const k Bool)
)";

// The assertions of `commands` after the prelude, printed.
std::vector<std::string> printed(const std::string& commands) {
  Frontend frontend;
  std::vector<TermId> assertions;
  for (const auto& command : primo::smtlib::Parser(kPrelude + commands).parse_commands()) {
    frontend.process_command(command, assertions);
  }
  std::vector<std::string> result;
  for (const TermId assertion : assertions) {
    result.push_back(print_term(frontend.terms(), assertion));
  }
  return result;
}

// Printing, parsing the text and printing again gives the same text.
void round_trip_is_stable() {
  const std::vector<std::string> formulas = {
      "(forall ((x U) (y U)) (=> (p x y) (p y x)))",
      "(forall ((x U)) (exists ((y U)) (and (= (f y) x) (not (= y ?x0)))))",
      "(forall ((x U)) (! (forall ((x U)) (p x x)) :pattern ((f x))))",
      "(exists ((s |odd sort|)) (or (q s) k))",
      "(forall ((x U) (y U)) (= (ite (p x y) x y) (f |b c|)))",
      "(or k (forall ((x U)) (= x a)))",
  };
  std::string commands;
  for (const auto& formula : formulas) commands += "(assert " + formula + ")";
  const std::vector<std::string> first = printed(commands);
  assert(first.size() == formulas.size());
  std::string reprinted;
  for (const auto& text : first) reprinted += "(assert " + text + ")";
  assert(printed(reprinted) == first);
}

// Generated variable names avoid declared symbols; other names are quoted as
// SMT-LIB requires.
void names_are_fresh_and_quoted() {
  const std::string text = printed("(assert (forall ((x U)) (= x |b c|)))").front();
  assert(text.starts_with("(forall ((?x1 U)) "));
  assert(text.find("|b c|") != std::string::npos);
  const std::string sorted = printed("(assert (forall ((s |odd sort|)) (q s)))").front();
  assert(sorted.find("(?x1 |odd sort|)") != std::string::npos);
}

// Printed assertions give the same answers as the originals.
void answers_survive_printing() {
  for (const std::string problem :
       {"(assert (forall ((x U) (y U)) (= x y))) (assert (not (= a |b c|)))",
        "(assert (exists ((x U)) (p x a)))"}) {
    const auto original = primo::smt::check_sat_smtlib(kPrelude + problem + "(check-sat)");
    std::string commands;
    for (const auto& text : printed(problem)) commands += "(assert " + text + ")";
    const auto reprinted = primo::smt::check_sat_smtlib(kPrelude + commands + "(check-sat)");
    assert(original.result == reprinted.result);
  }
}

// Open and arithmetic terms have no text in the quantified UF fragment.
void open_and_arithmetic_terms_are_rejected() {
  Frontend frontend;
  std::vector<TermId> assertions;
  for (const auto& command : primo::smtlib::Parser(kPrelude).parse_commands()) {
    frontend.process_command(command, assertions);
  }
  auto& terms = frontend.terms();
  const auto u = *terms.symbols().lookup_sort("U");
  bool threw = false;
  try {
    static_cast<void>(print_term(terms, terms.mk_bound_variable(0, u)));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);
  threw = false;
  try {
    static_cast<void>(print_term(terms, terms.mk_rational(primo::util::Rational{})));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);
}

}  // namespace

int main() {
  round_trip_is_stable();
  names_are_fresh_and_quoted();
  answers_survive_printing();
  open_and_arithmetic_terms_are_rejected();
  return 0;
}
