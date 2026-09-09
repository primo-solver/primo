#pragma once

#include <ostream>

namespace primo::smt {
struct CommandResponse;
}

namespace primo::cli {
void print_response(std::ostream& output, const smt::CommandResponse& response);
}  // namespace primo::cli
