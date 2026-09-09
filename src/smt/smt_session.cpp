#include "smt/smt_session.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/ids.hpp"
#include "core/term_manager.hpp"
#include "smt/check_sat.hpp"
#include "smt/frontend.hpp"
#include "smt/session_portfolio.hpp"
#include "smt/solver_session.hpp"
#include "smtlib/ast.hpp"
#include "smtlib/commands.hpp"
#include "smtlib/parser.hpp"
#include "util/statistics.hpp"

namespace primo::smt {
namespace {
using Clock = std::chrono::steady_clock;
using Command = smtlib::Command;
using Kind = smtlib::CommandKind;

util::Statistics::Value micros(Clock::time_point start) {
  return static_cast<util::Statistics::Value>(
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
}
}  // namespace

struct SmtSession::Implementation final : SessionExecutor {
  struct Entry {
    std::size_t depth;
    Command command;
  };

  explicit Implementation(CheckSatOptions supplied) : options(std::move(supplied)) {}

  CheckSatOptions options;
  std::optional<Command> logic;
  std::vector<Entry> journal;
  std::size_t depth = 0;
  std::size_t queries = 0;
  std::size_t rebuilds = 0;
  bool produce_models = false;
  bool print_success = false;
  bool stopped = false;
  std::unique_ptr<Frontend> frontend;
  std::vector<core::TermId> assertions;
  std::shared_ptr<const CheckSatOutcome> last;
  util::Statistics::Value parse_us = 0;
  util::Statistics::Value frontend_us = 0;

  CommandResponse response(ResponseKind kind, std::string message = {}) const {
    return {kind, print_success, std::move(message), nullptr};
  }

  void discard_translation() {
    assertions.clear();
    frontend.reset();
  }

  void ensure_frontend() {
    if (frontend) return;
    const auto start = Clock::now();
    frontend = std::make_unique<Frontend>();
    if (logic) frontend->process_command(*logic, assertions);
    for (const auto& entry : journal) frontend->process_command(entry.command, assertions);
    ++rebuilds;
    frontend_us += micros(start);
  }

  void validate_parameterized_definition(const Command& command) const {
    if (command.kind != Kind::DefineFun || command.parameters.empty()) return;
    // Existing macros expand on use. Validate in an isolated generation so
    // unused/forward-referencing bodies fail now without perturbing query IDs.
    Frontend validation;
    std::vector<core::TermId> roots;
    validation.process_command(*logic, roots);
    for (const auto& entry : journal) {
      if (entry.command.kind != Kind::Assert) validation.process_command(entry.command, roots);
    }
    validation.process_command(command, roots);
    std::vector<smtlib::SExpr> application{smtlib::SExpr::make_atom(command.name)};
    for (const auto& parameter : command.parameters) {
      const auto sort = validation.terms().symbols().lookup_sort(parameter.sort);
      if (!sort) throw std::invalid_argument("unknown parameter sort: " + parameter.sort);
      const auto term = validation.fresh_internal_constant(*sort, "definition_check");
      const auto symbol = validation.terms().get(term).symbol;
      application.push_back(
          smtlib::SExpr::make_atom(validation.terms().symbols().get_symbol(symbol).name));
    }
    auto call = smtlib::SExpr::make_list(std::move(application));
    Command check;
    check.kind = Kind::Assert;
    check.assertion = smtlib::SExpr::make_list({smtlib::SExpr::make_atom("="), call, call});
    validation.process_command(check, roots);
  }

  // Portfolio policy is shared with the one-shot API. Each arm consumes its
  // own frontend; a fallback replays the original journal, never a solved goal.
  CheckSatOutcome execute(std::string_view, CheckSatOptions selected) override {
    ensure_frontend();
    auto roots = std::move(assertions);
    assertions.clear();
    return check_sat_frontend_once(std::move(frontend), std::move(roots), std::move(selected));
  }

  CommandResponse apply(Command command) {
    if (command.kind == Kind::Unsupported) {
      return response(ResponseKind::Unsupported, "unsupported command: " + command.name);
    }
    if (command.kind == Kind::Exit) {
      stopped = true;
      return response(ResponseKind::Exit);
    }
    if (command.kind == Kind::Ignored) return response(ResponseKind::Success);
    if (command.kind == Kind::Reset) {
      logic.reset();
      journal.clear();
      depth = 0;
      last.reset();
      discard_translation();
      produce_models = false;
      print_success = false;
      return response(ResponseKind::Success);
    }
    if (command.kind == Kind::SetOption) {
      const bool start_only =
          command.option == ":produce-models" || command.option == ":global-declarations";
      if (start_only && logic) throw std::invalid_argument(command.option + " requires start mode");
      if (start_only || command.option == ":print-success") {
        if (command.value != "true" && command.value != "false") {
          throw std::invalid_argument(command.option + " expects true or false");
        }
        const bool enabled = command.value == "true";
        if (command.option == ":global-declarations" && enabled) {
          return response(ResponseKind::Unsupported, "global declarations are not supported");
        }
        if (command.option == ":produce-models") produce_models = enabled;
        if (command.option == ":print-success") print_success = enabled;
        return response(ResponseKind::Success);
      }
      return response(ResponseKind::Unsupported, "unsupported option: " + command.option);
    }
    if (command.kind == Kind::SetLogic) {
      if (logic) throw std::invalid_argument("set-logic requires start mode; use reset first");
      if (command.logic != "QF_UF" && command.logic != "QF_LRA" && command.logic != "QF_UFLRA" &&
          command.logic != "QF_LIA" && command.logic != "QF_UFLIA") {
        return response(ResponseKind::Unsupported, "unsupported logic: " + command.logic);
      }
      discard_translation();
      logic = std::move(command);
      return response(ResponseKind::Success);
    }
    if (command.kind == Kind::ResetAssertions) {
      journal.clear();
      depth = 0;
      last.reset();
      discard_translation();
      return response(ResponseKind::Success);
    }
    if (!logic) throw std::invalid_argument("command requires set-logic");
    if (command.kind == Kind::Push || command.kind == Kind::Pop) {
      if (command.kind == Kind::Push) {
        if (command.count > std::numeric_limits<std::size_t>::max() - depth) {
          throw std::invalid_argument("scope depth overflow");
        }
        depth += command.count;
      } else {
        if (command.count > depth)
          throw std::invalid_argument("cannot pop the base assertion level");
        depth -= command.count;
        while (!journal.empty() && journal.back().depth > depth) journal.pop_back();
        if (command.count != 0) discard_translation();
      }
      last.reset();
      return response(ResponseKind::Success);
    }
    if (command.kind == Kind::GetModel) {
      if (!produce_models) throw std::invalid_argument("get-model requires :produce-models true");
      if (!last || last->result != CheckSatResult::Sat || !last->typed_model) {
        throw std::invalid_argument("get-model requires a current SAT model");
      }
      return {ResponseKind::Model, print_success, {}, last};
    }
    if (command.kind == Kind::CheckSat) {
      last.reset();
      CheckSatOptions selected = options;
      selected.produce_model = produce_models || selected.produce_model;
      auto outcome = selected.qf_uf_portfolio == QfUfPortfolioMode::Off
                         ? execute({}, selected)
                         : execute_qf_uf_session_portfolio({}, selected, *this);
      ++queries;
      outcome.statistics.set(":session.queries", queries);
      outcome.statistics.set(":session.frontend-builds", rebuilds);
      outcome.statistics.set(":time.parse-us", parse_us);
      outcome.statistics.set(":time.frontend-us", frontend_us);
      outcome.statistics.set(":time.total-us",
                             outcome.statistics.get(":time.total-us") + parse_us + frontend_us);
      parse_us = 0;
      frontend_us = 0;
      last = std::make_shared<CheckSatOutcome>(std::move(outcome));
      return {ResponseKind::CheckSat, print_success, last->message, last};
    }
    ensure_frontend();
    const auto start = Clock::now();
    validate_parameterized_definition(command);
    frontend->process_command(command, assertions);
    frontend_us += micros(start);
    journal.push_back({depth, std::move(command)});
    last.reset();
    return response(ResponseKind::Success);
  }
};

SmtSession::SmtSession(CheckSatOptions options)
    : implementation_(std::make_unique<Implementation>(std::move(options))) {}
SmtSession::~SmtSession() = default;
bool SmtSession::terminated() const { return implementation_->stopped; }

CommandResponse SmtSession::execute(std::string_view text) {
  auto& state = *implementation_;
  if (state.stopped) return state.response(ResponseKind::Error, "session has terminated");
  try {
    const auto start = Clock::now();
    auto commands = smtlib::Parser(text).parse_commands();
    state.parse_us += micros(start);
    if (commands.size() != 1) throw std::invalid_argument("execute expects exactly one command");
    return state.apply(std::move(commands.front()));
  } catch (const std::exception& error) {
    state.stopped = true;
    state.last.reset();
    state.discard_translation();
    return state.response(ResponseKind::Error, error.what());
  }
}

}  // namespace primo::smt
