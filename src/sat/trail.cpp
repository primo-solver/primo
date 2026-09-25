#include "sat/trail.hpp"

#include <limits>
#include <stdexcept>

namespace primo::sat {
namespace {
std::size_t variable(int literal) {
  if (literal == 0 || literal == std::numeric_limits<int>::min()) {
    throw std::logic_error("invalid assignment literal");
  }
  return static_cast<std::size_t>(literal < 0 ? -literal : literal);
}
}  // namespace

std::size_t Trail::position(int var) const {
  if (var <= 0) throw std::logic_error("assignment lookup requires a positive variable");
  const auto index = static_cast<std::size_t>(var);
  return index < positions_.size() ? positions_[index] : unassigned;
}

bool Trail::assign(int lit) {
  const auto var = variable(lit);
  if (var >= positions_.size()) positions_.resize(var + 1, unassigned);
  if (positions_[var] != unassigned) {
    if (literals_[positions_[var]] != lit) {
      throw std::logic_error("opposite assignment without backtracking");
    }
    return false;
  }
  literals_.push_back(lit);
  positions_[var] = literals_.size() - 1;
  ++epoch_;
  return true;
}

void Trail::push_level() {
  boundaries_.push_back(literals_.size());
  ++epoch_;
}

void Trail::backtrack(std::size_t level, std::vector<int>& removed) {
  removed.clear();
  if (level > boundaries_.size()) throw std::logic_error("backtrack beyond current level");
  if (level == boundaries_.size()) return;
  const auto keep = boundaries_[level];
  // Own the event payload before truncation; observers may grow other storage.
  removed.reserve(literals_.size() - keep);
  for (std::size_t i = literals_.size(); i > keep; --i) {
    const auto lit = literals_[i - 1];
    removed.push_back(lit);
    positions_[variable(lit)] = unassigned;
  }
  literals_.resize(keep);
  boundaries_.resize(level);
  ++epoch_;
}

void Trail::clear() {
  for (const auto lit : literals_) positions_[variable(lit)] = unassigned;
  literals_.clear();
  boundaries_.clear();
  ++epoch_;
}

void Trail::load_model(std::span<const int> model) {
  clear();
  for (const auto lit : model) assign(lit);
}
}  // namespace primo::sat
