#pragma once

#include <string>
#include <vector>

#include "smtlib/ast.hpp"

namespace primo::smtlib {

enum class CommandKind {
  SetLogic,
  DeclareSort,
  DeclareFun,
  Assert,
  CheckSat,
};

struct Command {
  CommandKind kind = CommandKind::CheckSat;
  std::string name;
  std::string logic;
  std::vector<std::string> arg_sorts;
  std::string result_sort;
  SExpr assertion;
};

}  // namespace primo::smtlib
