#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "sat/trail.hpp"

namespace primo::sat {

// Borrow the owner, never its growable buffers. The owner must outlive the view.
class TrailView {
 public:
  TrailView() = default;
  explicit TrailView(const Trail& trail) : trail_(&trail) {}
  [[nodiscard]] bool attached() const { return trail_ != nullptr; }
  [[nodiscard]] int literal(int variable) const { return owner().literal(variable); }
  [[nodiscard]] std::size_t position(int variable) const { return owner().position(variable); }
  [[nodiscard]] int at(std::size_t index) const { return owner().at(index); }
  [[nodiscard]] std::size_t size() const { return owner().size(); }
  [[nodiscard]] std::size_t level() const { return owner().level(); }
  [[nodiscard]] std::uint64_t epoch() const { return owner().epoch(); }

 private:
  const Trail& owner() const {
    if (trail_ == nullptr) throw std::logic_error("assignment view is not bound");
    return *trail_;
  }
  const Trail* trail_ = nullptr;
};

}  // namespace primo::sat
