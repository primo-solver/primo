#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "smt/check_sat.hpp"
#include "smtlib/commands.hpp"

namespace primo::smt {

enum class ResponseKind { Success, CheckSat, Model, Error, Unsupported, Exit };

struct CommandResponse {
  ResponseKind kind = ResponseKind::Success;
  bool print_success = false;
  std::string message;
  std::shared_ptr<const CheckSatOutcome> outcome;
};

// Scoped command interpreter. The journal, not a transformed Goal, is the
// authoritative context. See docs/incremental-smt.md for replay/lifetime rules.
class SmtSession {
 public:
  explicit SmtSession(CheckSatOptions options = {});
  ~SmtSession();
  SmtSession(const SmtSession&) = delete;
  SmtSession& operator=(const SmtSession&) = delete;

  CommandResponse execute(std::string_view command);
  // Consume parsed syntax without serializing/reparsing it. Parsing duration
  // is supplied by the caller; a live stream's wall time includes input waits.
  CommandResponse execute(smtlib::Command command, std::uint64_t parse_microseconds = 0);
  bool terminated() const;

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace primo::smt
