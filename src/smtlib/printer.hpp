#pragma once

#include <string>

namespace primo::core {
class Model;
}

namespace primo::smtlib {

// Serializes an already finalized interpretation; never constructs model values.
std::string print_model(const core::Model& model);

}  // namespace primo::smtlib
