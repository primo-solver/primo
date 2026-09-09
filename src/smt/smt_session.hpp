#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "smt/check_sat.hpp"

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
  bool terminated() const;

 private:
  struct Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace primo::smt
