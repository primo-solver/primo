#pragma once

#include <span>

#include "smtlib/ast.hpp"

namespace primo::smtlib {

// SMT-LIB 2.7 Sections 3.4/3.6, generic syntax only. List values are opaque
// S-expressions, not terms. These queries allocate nothing and retain no borrow.
bool is_attribute_value(const SExpr& expr);
bool valid_attributes(std::span<const SExpr> attributes);

}  // namespace primo::smtlib
