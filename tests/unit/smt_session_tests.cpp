#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "cli/response_printer.hpp"
#include "smt/check_sat.hpp"
#include "smt/smt_session.hpp"
#include "smtlib/parser.hpp"

using primo::smt::CheckSatResult;
using primo::smt::ResponseKind;
using primo::smt::SmtSession;

namespace {
void accept(SmtSession& session, const std::string& text) {
  const auto response = session.execute(text);
  if (response.kind != ResponseKind::Success)
    throw std::runtime_error("command failed: " + text + ": " + response.message);
}

void expect(SmtSession& session, CheckSatResult result) {
  const auto response = session.execute("(check-sat)");
  assert(response.kind == ResponseKind::CheckSat);
  assert(response.outcome->result == result);
}

void uf_assumption_scopes_and_preparation() {
  for (const auto preprocessing :
       {primo::smt::PreprocessingMode::Off, primo::smt::PreprocessingMode::Auto}) {
    for (const auto propagation :
         {primo::smt::EufPropagationMode::Online, primo::smt::EufPropagationMode::Conflict}) {
      primo::smt::CheckSatOptions options;
      options.incremental_mode = primo::smt::IncrementalMode::Assumptions;
      options.preprocessing_mode = preprocessing;
      options.euf_propagation = propagation;
      options.produce_model = true;
      options.check_model = true;
      SmtSession session(options);
      accept(session, "(set-option :produce-models true)");
      accept(session, "(set-logic QF_UF)");
      accept(session, "(declare-sort U 0)");
      accept(session, "(declare-const a U)");
      accept(session, "(declare-const b U)");
      accept(session, "(declare-const p Bool)");
      accept(session, "(declare-fun f (U U) U)");
      accept(session, "(declare-fun g (Bool) U)");
      accept(session, "(define-fun d () U (ite p (g (not p)) a))");
      accept(session, "(assert (= a b))");
      expect(session, CheckSatResult::Sat);
      const auto saved = session.execute("(get-model)").outcome;
      accept(session, "(push 2)");
      accept(session, "(assert (distinct (f a (f a b)) (f b (f b a))))");
      expect(session, CheckSatResult::Unsat);
      accept(session, "(pop 1)");
      expect(session, CheckSatResult::Sat);
      accept(session, "(declare-sort Local 0)");
      accept(session, "(declare-const local Local)");
      accept(session, "(assert p)");
      accept(session, "(assert (= d (g false)))");
      expect(session, CheckSatResult::Sat);
      const auto scoped = session.execute("(get-model)").outcome;
      accept(session, "(assert (distinct (g p) (g true)))");
      expect(session, CheckSatResult::Unsat);
      accept(session, "(pop 1)");
      accept(session, "(declare-sort Local 0)");
      accept(session, "(declare-const local Bool)");
      accept(session, "(assert local)");
      accept(session, "(push 1)");
      accept(session, "(assert (not p))");
      accept(session, "(assert (= d a))");
      expect(session, CheckSatResult::Sat);
      accept(session, "(pop 1)");
      accept(session, "(assert p)");
      accept(session, "(assert (distinct d (g false)))");
      expect(session, CheckSatResult::Unsat);
      const auto repeated = session.execute("(check-sat)").outcome;
      assert(repeated->statistics.get(":session.engine-builds") == 1);
      assert(repeated->statistics.get(":session.frontend-builds") == 1);
      assert(repeated->statistics.get(":session.euf-new-prepared-atoms") == 0);
      assert(repeated->statistics.get(":session.euf-registration-atoms-scanned") == 0);
      assert(repeated->statistics.get(":session.euf-preparation-atoms-scanned") == 0);
      assert(repeated->statistics.get(":session.retired-selectors") == 3);
      accept(session, "(reset)");
      assert(saved->model.find("local") == std::string::npos);
      assert(scoped->model.find("local () Local") != std::string::npos);
    }
  }
}

void uf_assumption_atom_scans_follow_only_registry_growth() {
  for (const auto preprocessing :
       {primo::smt::PreprocessingMode::Off, primo::smt::PreprocessingMode::Auto}) {
    primo::smt::CheckSatOptions options;
    options.incremental_mode = primo::smt::IncrementalMode::Assumptions;
    options.preprocessing_mode = preprocessing;
    options.produce_model = true;
    options.check_model = true;
    SmtSession session(options);
    accept(session, "(set-logic QF_UF)");
    accept(session, "(declare-sort U 0)");
    accept(session, "(declare-fun f (Bool) U)");
    accept(session, "(declare-const p Bool)");
    accept(session, "(assert p)");
    std::uint64_t retained = 0;
    const auto check = [&](CheckSatResult expected) {
      const auto outcome = session.execute("(check-sat)").outcome;
      assert(outcome->result == expected);
      const auto after = outcome->statistics.get(":session.retained-atoms");
      assert(outcome->statistics.get(":session.euf-registration-atoms-scanned") ==
             after - retained);
      assert(outcome->statistics.get(":session.euf-preparation-atoms-scanned") == after - retained);
      retained = after;
    };
    check(CheckSatResult::Sat);
    // An old atom becomes a Bool argument only after its root was scanned.
    accept(session, "(push 1)");
    accept(session, "(assert (distinct (f p) (f true)))");
    check(CheckSatResult::Unsat);
    accept(session, "(pop 1)");
    check(CheckSatResult::Sat);
    for (int i = 0; i < 96; ++i) {
      accept(session, "(push 1)");
      accept(session, "(declare-const q Bool)");
      // Registration grows the registry itself; each redeclaration has a fresh ID.
      accept(session, "(assert (distinct (f q) (f false)))");
      check(CheckSatResult::Sat);
      check(CheckSatResult::Sat);
      accept(session, "(assert (not q))");
      check(CheckSatResult::Unsat);
      accept(session, "(pop 1)");
      check(CheckSatResult::Sat);
    }
    // The retained f(p) root is reused without rescanning or losing registration.
    accept(session, "(assert (distinct (f p) (f true)))");
    check(CheckSatResult::Unsat);
    accept(session, "(reset)");
    retained = 0;
    accept(session, "(set-logic QF_UF)");
    accept(session, "(declare-const p Bool)");
    accept(session, "(assert p)");
    check(CheckSatResult::Sat);
  }
}

void uf_assumption_growth_cancellation_and_options() {
  primo::smt::CheckSatOptions options;
  options.incremental_mode = primo::smt::IncrementalMode::Assumptions;
  options.produce_model = true;
  options.check_model = true;
  bool cancelled = true;
  options.cancelled = [&cancelled] { return cancelled; };
  SmtSession session(options);
  accept(session, "(set-logic QF_UF)");
  accept(session, "(declare-const p Bool)");
  expect(session, CheckSatResult::Unknown);
  cancelled = false;
  expect(session, CheckSatResult::Sat);
  for (int i = 0; i < 64; ++i) {
    accept(session, "(push 1)");
    accept(session, "(declare-sort U 0)");
    accept(session, "(declare-fun f (Bool) U)");
    accept(session, "(define-fun d () U (f (not p)))");
    accept(session, "(assert (distinct (f p) d))");
    expect(session, CheckSatResult::Sat);
    accept(session, "(assert (= (f true) (f false)))");
    expect(session, CheckSatResult::Unsat);
    accept(session, "(pop 1)");
  }
  const auto final = session.execute("(check-sat)").outcome;
  assert(final->result == CheckSatResult::Sat);
  assert(final->typed_model->visible_symbols().size() == 1);
  assert(final->statistics.get(":session.retired-selectors") == 64);
  accept(session, "(reset-assertions)");
  accept(session, "(declare-const p Bool)");
  expect(session, CheckSatResult::Sat);
  assert(session.execute("(pop 1)").kind == ResponseKind::Error);
  options.max_theory_conflicts = 1;
  options.euf_propagation = primo::smt::EufPropagationMode::Conflict;
  SmtSession bounded(options);
  accept(bounded, "(set-logic QF_UF)");
  accept(bounded, "(declare-sort U 0)");
  accept(bounded, "(declare-const a U)");
  accept(bounded, "(declare-const b U)");
  accept(bounded, "(declare-fun f (U) U)");
  accept(bounded, "(assert (= a b))");
  accept(bounded, "(push 1)");
  accept(bounded, "(assert (distinct (f a) (f b)))");
  expect(bounded, CheckSatResult::Unknown);
  accept(bounded, "(pop 1)");
  expect(bounded, CheckSatResult::Sat);
  for (int invalid = 0; invalid < 4; ++invalid) {
    auto rejected_options = options;
    if (invalid == 0) rejected_options.pure_theory_filter = primo::smt::PureTheoryFilterMode::On;
    if (invalid == 1) rejected_options.euf_engine = primo::smt::EufEngine::Lazy;
    if (invalid == 2)
      rejected_options.euf_boolean_domain = primo::smt::EufBooleanDomain::BridgeAtoms;
    if (invalid == 3) rejected_options.euf_propagation = primo::smt::EufPropagationMode::Model;
    SmtSession rejected(rejected_options);
    assert(rejected.execute("(set-logic QF_UF)").kind == ResponseKind::Error);
  }
}

void uf_assumption_models_use_active_lowered_roots() {
  for (const auto preprocessing :
       {primo::smt::PreprocessingMode::Off, primo::smt::PreprocessingMode::Auto}) {
    primo::smt::CheckSatOptions options;
    options.incremental_mode = primo::smt::IncrementalMode::Assumptions;
    options.preprocessing_mode = preprocessing;
    options.produce_model = true;
    options.check_model = true;
    SmtSession session(options);
    accept(session, "(set-option :produce-models true)");
    accept(session, "(set-logic QF_UF)");
    accept(session, "(declare-sort U 0)");
    accept(session, "(declare-const a U)");
    accept(session, "(declare-const c U)");
    accept(session, "(declare-const b Bool)");
    accept(session, "(declare-fun f (U) Bool)");
    expect(session, CheckSatResult::Sat);
    accept(session, "(push 2)");
    accept(session, "(assert (f (ite b a c)))");
    expect(session, CheckSatResult::Sat);
    const auto saved = session.execute("(get-model)").outcome;
    const auto saved_text = saved->model;
    expect(session, CheckSatResult::Sat);
    accept(session, "(assert (not (f (ite b a c))))");
    expect(session, CheckSatResult::Unsat);
    accept(session, "(pop 1)");
    const auto empty = session.execute("(check-sat)").outcome;
    assert(empty->result == CheckSatResult::Sat);
    assert(empty->statistics.get(":session.active-model-roots") == 0);
    accept(session, "(push 1)");
    accept(session, "(assert (not (f (ite b a c))))");
    expect(session, CheckSatResult::Sat);
    accept(session, "(pop 2)");
    expect(session, CheckSatResult::Sat);
    accept(session, "(reset)");
    assert(saved->model == saved_text);
    assert(saved->typed_model != nullptr);
  }
}

void assumption_mode_scopes_and_reuse() {
  for (const auto mode :
       {primo::smt::PreprocessingMode::Off, primo::smt::PreprocessingMode::Auto}) {
    primo::smt::CheckSatOptions options;
    options.incremental_mode = primo::smt::IncrementalMode::Assumptions;
    options.preprocessing_mode = mode;
    options.check_model = true;
    SmtSession session(options);
    accept(session, "(set-option :produce-models true)");
    accept(session, "(set-logic QF_LRA)");
    accept(session, "(declare-const x Real)");
    accept(session, "(declare-const b Bool)");
    accept(session, "(define-fun f ((v Real)) Real (+ x v))");
    accept(session, "(define-fun d () Real (ite b (f 1) (f 2)))");
    accept(session, "(assert (>= x 0))");
    expect(session, CheckSatResult::Sat);
    const auto saved = session.execute("(get-model)").outcome;
    accept(session, "(push 2)");
    accept(session, "(assert (< x 0))");
    expect(session, CheckSatResult::Unsat);
    accept(session, "(pop 1)");
    expect(session, CheckSatResult::Sat);
    accept(session, "(declare-const y Real)");
    accept(session, "(assert (= y d))");
    accept(session, "(assert b)");
    expect(session, CheckSatResult::Sat);
    const auto scoped = session.execute("(get-model)").outcome;
    accept(session, "(assert (<= y x))");
    expect(session, CheckSatResult::Unsat);
    accept(session, "(pop 1)");
    accept(session, "(declare-const y Bool)");
    accept(session, "(assert y)");
    accept(session, "(push 1)");
    accept(session, "(assert (> d x))");
    expect(session, CheckSatResult::Sat);
    accept(session, "(pop 1)");
    accept(session, "(assert (<= d x))");
    expect(session, CheckSatResult::Unsat);
    const auto last = session.execute("(check-sat)").outcome;
    assert(last->statistics.get(":session.frontend-builds") == 1);
    assert(last->statistics.get(":session.engine-builds") == 1);
    assert(last->statistics.get(":session.retired-selectors") == 3);
    assert(last->statistics.get(":session.active-selectors") == 0);
    assert(last->statistics.get(":session.translated-assertions") == 8);
    accept(session, "(reset)");
    assert(saved->model.find("y") == std::string::npos);
    assert(scoped->model.find("y () Real") != std::string::npos);
  }
}

void assumption_mode_cancellation_and_options() {
  bool cancelled = true;
  primo::smt::CheckSatOptions options;
  options.incremental_mode = primo::smt::IncrementalMode::Assumptions;
  options.cancelled = [&cancelled] { return cancelled; };
  SmtSession session(options);
  accept(session, "(set-logic QF_LRA)");
  accept(session, "(declare-const x Real)");
  accept(session, "(assert (> x 1))");
  expect(session, CheckSatResult::Unknown);
  cancelled = false;
  expect(session, CheckSatResult::Sat);
  accept(session, "(push 1)");
  accept(session, "(assert (< x 1))");
  expect(session, CheckSatResult::Unsat);
  accept(session, "(pop 1)");
  expect(session, CheckSatResult::Sat);
  accept(session, "(reset-assertions)");
  accept(session, "(declare-const x Bool)");
  accept(session, "(assert x)");
  expect(session, CheckSatResult::Sat);
  assert(session.execute("(pop 1)").kind == ResponseKind::Error);
  for (const auto mode :
       {primo::smt::PreprocessingMode::Basic, primo::smt::PreprocessingMode::Strong}) {
    options.preprocessing_mode = mode;
    SmtSession rejected(options);
    assert(rejected.execute("(set-logic QF_LRA)").kind == ResponseKind::Error);
  }
  options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  for (const std::string invalid :
       {"(define-fun f ((v Real)) Bool (+ v 1))", "(define-fun f ((v Real)) Real (+ v future))"}) {
    SmtSession definitions(options);
    accept(definitions, "(set-logic QF_LRA)");
    assert(definitions.execute(invalid).kind == ResponseKind::Error);
  }
  options.preprocessing_mode = primo::smt::PreprocessingMode::Auto;
  SmtSession rejected(options);
  assert(rejected.execute("(set-logic QF_UFLRA)").kind == ResponseKind::Unsupported);
}

void assumption_scope_storage_growth_and_budget() {
  primo::smt::CheckSatOptions options;
  options.incremental_mode = primo::smt::IncrementalMode::Assumptions;
  options.produce_model = true;
  options.check_model = true;
  SmtSession session(options);
  accept(session, "(set-logic QF_LRA)");
  accept(session, "(declare-const base Real)");
  accept(session, "(assert (>= base 0))");
  for (int i = 0; i < 64; ++i) {
    accept(session, "(push 1)");
    accept(session, "(declare-const reused Real)");
    accept(session, "(define-fun delta () Real (+ base " + std::to_string(i + 1) + "))");
    accept(session, "(assert (= reused delta))");
    expect(session, CheckSatResult::Sat);
    accept(session, "(pop 1)");
  }
  const auto final = session.execute("(check-sat)").outcome;
  assert(final->typed_model->visible_symbols().size() == 1);
  assert(final->statistics.get(":session.engine-builds") == 1);
  assert(final->statistics.get(":session.retired-selectors") == 64);
  assert(final->model.find("reused") == std::string::npos);
  assert(session.execute("(assert (= reused base))").kind == ResponseKind::Error);

  options.max_theory_conflicts = 1;
  options.lra_propagation = false;
  SmtSession bounded(options);
  accept(bounded, "(set-logic QF_LRA)");
  accept(bounded, "(declare-const x Real)");
  accept(bounded, "(assert (>= x 0))");
  accept(bounded, "(push 1)");
  accept(bounded, "(assert (< x 0))");
  expect(bounded, CheckSatResult::Unknown);
  accept(bounded, "(pop 1)");
  expect(bounded, CheckSatResult::Sat);
}

void scopes_and_unsafe_simplification() {
  for (const std::string logic : {"QF_LIA", "QF_LRA", "QF_UFLIA", "QF_UFLRA"}) {
    primo::smt::CheckSatOptions options;
    options.check_model = true;
    SmtSession session(options);
    accept(session, "(set-option :produce-models true)");
    accept(session, "(set-logic " + logic + ")");
    const std::string sort = logic.ends_with("LIA") ? "Int" : "Real";
    accept(session, "(declare-const x " + sort + ")");
    accept(session, "(assert (>= x 0))");
    expect(session, CheckSatResult::Sat);
    const auto saved = session.execute("(get-model)").outcome;
    assert(saved && saved->typed_model);
    accept(session, "(push 2)");
    accept(session, "(assert (<= x (- 1)))");
    expect(session, CheckSatResult::Unsat);
    accept(session, "(pop 1)");
    expect(session, CheckSatResult::Sat);
    accept(session, "(declare-const y " + sort + ")");
    accept(session, "(assert (= y (+ x 1)))");
    expect(session, CheckSatResult::Sat);
    accept(session, "(assert (<= y x))");
    expect(session, CheckSatResult::Unsat);
    accept(session, "(pop 1)");
    expect(session, CheckSatResult::Sat);
    // Old models own their signature even after both translation generations die.
    assert(saved->model.find("x") != std::string::npos);
    assert(saved->model.find("y") == std::string::npos);
  }
}

void declarations_definitions_and_side_constraints() {
  SmtSession session;
  accept(session, "(set-logic QF_UF)");
  accept(session, "(declare-const b Bool)");
  accept(session, "(push 1)");
  accept(session, "(declare-sort U 0)");
  accept(session, "(declare-fun f (Bool) Bool)");
  accept(session, "(define-fun d () Bool (f (not b)))");
  accept(session, "(assert d)");
  expect(session, CheckSatResult::Sat);
  accept(session, "(pop 1)");
  accept(session, "(push 1)");
  accept(session, "(declare-sort U 0)");
  accept(session, "(declare-fun f (Bool) Bool)");
  accept(session, "(define-fun d () Bool (f (not b)))");
  accept(session, "(assert (not d))");
  accept(session, "(assert (f (not b)))");
  expect(session, CheckSatResult::Unsat);
  accept(session, "(pop 1)");
  accept(session, "(declare-const d Bool)");
  expect(session, CheckSatResult::Sat);
  accept(session, "(reset-assertions)");
  accept(session, "(declare-sort U 0)");
  accept(session, "(declare-const b U)");
  expect(session, CheckSatResult::Sat);
  accept(session, "(reset)");
  accept(session, "(set-logic QF_LIA)");
  accept(session, "(declare-const b Int)");
  expect(session, CheckSatResult::Sat);
}

void protocol_errors() {
  for (const std::string command :
       {"(pop 1)", "(push -1)", "(push 01)", "(pop)", "(push 184467440737095516160)", "(get-model)",
        "(reset 1)", "(exit 2)", "(set-logic QF_LIA)", "(set-option :produce-models true)"}) {
    SmtSession session;
    accept(session, "(set-logic QF_UF)");
    assert(session.execute(command).kind == ResponseKind::Error);
    assert(session.terminated());
  }
  for (const std::string command : {"(declare-const d Bool)", "(define-fun d () Bool false)"}) {
    SmtSession session;
    accept(session, "(set-logic QF_UF)");
    accept(session, "(define-fun d () Bool true)");
    assert(session.execute(command).kind == ResponseKind::Error);
  }
  SmtSession session;
  assert(session.execute("(set-option :global-declarations true)").kind ==
         ResponseKind::Unsupported);
  accept(session, "(set-option :global-declarations false)");
  accept(session, "(set-option :produce-models true)");
  accept(session, "(set-logic QF_UF)");
  expect(session, CheckSatResult::Sat);
  assert(session.execute("(get-model)").kind == ResponseKind::Model);
  accept(session, "(push 0)");
  accept(session, "(pop 0)");
  assert(session.execute("(get-model)").kind == ResponseKind::Error);
}

void quoting_and_responses() {
  std::istringstream input(R"(; outside
    (set-info :source "a ""quoted"" ); text")
    (set-logic QF_UF)(declare-const |p(;)| Bool)
    (assert |p(;)|)(check-sat)(exit) garbage ignored
  )");
  primo::smtlib::Parser parser(input);
  SmtSession session;
  std::ostringstream output;
  while (!session.terminated()) {
    auto command = parser.next_command();
    assert(command);
    auto response = session.execute(std::move(*command));
    assert(response.kind != ResponseKind::Error);
    primo::cli::print_response(output, response);
  }
  assert(output.str() == "sat\n");
  SmtSession success;
  auto response = success.execute("(set-option :print-success true)");
  assert(response.print_success);
  response = success.execute("(set-option :print-success false)");
  assert(!response.print_success);
}

void annotation_attribute_grammar() {
  for (const std::string attributes :
       {":flag", ":first :second", ":first 0 :second :third |let|", ":custom ()",
        ":custom (:key let (assert (! true bogus)))", ":custom #b01 :hex #x0f",
        ":custom \"a\"\"b\" :decimal 0.01 :quoted |:keyword|"}) {
    SmtSession session;
    accept(session, "(set-logic QF_UF)");
    accept(session, "(assert (! true " + attributes + "))");
    expect(session, CheckSatResult::Sat);
    accept(session, "(assert (! (! false :inner) " + attributes + "))");
    expect(session, CheckSatResult::Unsat);
  }
  for (const std::string term :
       {"(!)", "(! true)", "(! true bogus)", "(! true |:flag|)", "(! true (:flag))",
        "(! true :a 0 1)", "(! true :a let)", "(! true :a 0 bogus)", "(! (! true bogus) :outer)"}) {
    for (const std::string& command :
         {"(assert " + term + ")", "(define-fun f ((x Bool)) Bool " + term + ")"}) {
      SmtSession session;
      accept(session, "(set-logic QF_UF)");
      assert(session.execute(command).kind == ResponseKind::Error);
      assert(session.terminated());
    }
  }
}

void one_shot_rejects_multi_query() {
  const auto outcome =
      primo::smt::check_sat_smtlib("(set-logic QF_UF)(check-sat)(assert false)(check-sat)");
  assert(outcome.result == CheckSatResult::Unsupported);
}

void definition_validation_and_portfolio() {
  for (const std::string definition :
       {"(define-fun f ((x Bool)) Bool missing)", "(define-fun f ((x Bool)) Int x)",
        "(define-fun f ((x Bool) (x Bool)) Bool x)"}) {
    SmtSession session;
    accept(session, "(set-logic QF_UFLIA)");
    assert(session.execute(definition).kind == ResponseKind::Error);
  }
  primo::smt::CheckSatOptions options;
  options.experimental_split_bools = 2;
  options.check_model = true;
  options.qf_uf_portfolio = primo::smt::QfUfPortfolioMode::BasicPolarity;
  SmtSession session(options);
  accept(session, "(set-option :produce-models true)");
  accept(session, "(set-logic QF_UF)");
  accept(session, "(declare-const a Bool)");
  accept(session, "(declare-const b Bool)");
  accept(session, "(define-fun f ((x Bool)) Bool (not x))");
  accept(session, "(assert (or a b))");
  expect(session, CheckSatResult::Sat);
  accept(session, "(push 1)");
  accept(session, "(assert (and (f a) (f b)))");
  expect(session, CheckSatResult::Unsat);
  accept(session, "(pop 1)");
  expect(session, CheckSatResult::Sat);
}

void definitions_are_lexical_not_dynamic() {
  SmtSession session;
  accept(session, "(set-logic QF_UF)");
  accept(session, "(declare-const a Bool)");
  accept(session, "(define-fun f ((v Bool)) Bool a)");
  accept(session, "(define-fun g ((a Bool)) Bool (f a))");
  accept(session, "(assert (not a))");
  accept(session, "(push 1)");
  accept(session, "(assert (let ((a true)) (g a)))");
  expect(session, CheckSatResult::Unsat);
  accept(session, "(pop 1)");
  accept(session, "(define-fun h ((a Bool)) Bool (let ((z (f a))) (and (not z) a)))");
  accept(session, "(assert (h true))");
  expect(session, CheckSatResult::Sat);
}
void canonical_names_and_categories() {
  for (const bool reversed : {false, true}) {
    SmtSession session;
    accept(session, "(set-logic QF_UF)");
    const std::string global = reversed ? "|gate|" : "gate";
    const std::string local = reversed ? "gate" : "|gate|";
    accept(session, "(declare-const " + global + " Bool)");
    accept(session, "(assert " + global + ")");
    accept(session, "(assert (let ((" + local + " false)) " + global + "))");
    expect(session, CheckSatResult::Unsat);
  }
  SmtSession session;
  accept(session, "(set-logic QF_UF)");
  accept(session, "(declare-sort |U| 0)");
  accept(session, "(declare-const || U)");
  accept(session, "(declare-fun |let| (U) Bool)");
  accept(session, "(define-fun |same| ((|17| U)) Bool (|let| |17|))");
  accept(session, "(assert (same ||))");
  accept(session, "(push 1)");
  accept(session, "(declare-const |x| Bool)");
  accept(session, "(assert x)");
  expect(session, CheckSatResult::Sat);
  accept(session, "(pop 1)");
  accept(session, "(declare-const x Bool)");
  accept(session, "(assert (not |x|))");
  expect(session, CheckSatResult::Sat);
  accept(session, "(assert (not (|let| ||)))");
  expect(session, CheckSatResult::Unsat);

  SmtSession parallel;
  accept(parallel, "(set-logic QF_UF)");
  accept(parallel, "(declare-const x Bool)");
  accept(parallel, "(assert x)");
  accept(parallel, "(assert (let ((|x| false) (x x)) |x|))");
  expect(parallel, CheckSatResult::Sat);
  accept(parallel, "(assert (let ((x true) (|x| false)) x))");
  expect(parallel, CheckSatResult::Unsat);

  for (const std::string input : {"(declare-const |x| Bool)", "(define-fun |x| () Bool false)"}) {
    SmtSession duplicates;
    accept(duplicates, "(set-logic QF_UF)");
    accept(duplicates, "(declare-const x Bool)");
    assert(duplicates.execute(input).kind == ResponseKind::Error);
  }
  for (const std::string input :
       {"(assert \"false\")", "(assert let)", "(assert (let ((17 true)) true))",
        "(assert (\"and\" true false))"}) {
    SmtSession invalid;
    accept(invalid, "(set-logic QF_UF)");
    assert(invalid.execute(input).kind == ResponseKind::Error);
  }
}

}  // namespace

int main() {
  uf_assumption_scopes_and_preparation();
  uf_assumption_atom_scans_follow_only_registry_growth();
  uf_assumption_growth_cancellation_and_options();
  uf_assumption_models_use_active_lowered_roots();
  assumption_mode_scopes_and_reuse();
  assumption_scope_storage_growth_and_budget();
  assumption_mode_cancellation_and_options();
  annotation_attribute_grammar();
  canonical_names_and_categories();
  scopes_and_unsafe_simplification();
  declarations_definitions_and_side_constraints();
  protocol_errors();
  quoting_and_responses();
  one_shot_rejects_multi_query();
  definition_validation_and_portfolio();
  definitions_are_lexical_not_dynamic();
}
