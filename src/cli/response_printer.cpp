#include "cli/response_printer.hpp"

#include <ostream>

#include "smt/check_sat.hpp"
#include "smt/smt_session.hpp"

namespace primo::cli {

void print_response(std::ostream& output, const smt::CommandResponse& response) {
  switch (response.kind) {
    case smt::ResponseKind::Success:
    case smt::ResponseKind::Exit:
      if (response.print_success) output << "success\n";
      break;
    case smt::ResponseKind::Unsupported:
      output << "unsupported\n";
      break;
    case smt::ResponseKind::Error:
      output << "(error \"";
      for (const char c : response.message) {
        if (c == '"') output << '"';
        output << c;
      }
      output << "\")\n";
      break;
    case smt::ResponseKind::CheckSat:
      output << smt::to_string(response.outcome->result) << '\n';
      break;
    case smt::ResponseKind::Model:
      output << response.outcome->model << '\n';
      break;
  }
}

}  // namespace primo::cli
