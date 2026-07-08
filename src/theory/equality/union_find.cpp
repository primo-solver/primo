#include "theory/equality/union_find.hpp"

#include <numeric>
#include <stdexcept>

namespace primo::theory::equality {

UnionFind::UnionFind(std::size_t size) { reset(size); }

void UnionFind::reset(std::size_t size) {
  parent_.resize(size);
  rank_.assign(size, 0);
  std::iota(parent_.begin(), parent_.end(), 0);
}

std::size_t UnionFind::size() const { return parent_.size(); }

std::size_t UnionFind::find(std::size_t item) {
  if (item >= parent_.size()) {
    throw std::out_of_range("union-find item out of range");
  }
  if (parent_[item] != item) {
    parent_[item] = find(parent_[item]);
  }
  return parent_[item];
}

std::size_t UnionFind::find(std::size_t item) const {
  if (item >= parent_.size()) {
    throw std::out_of_range("union-find item out of range");
  }
  while (parent_[item] != item) {
    item = parent_[item];
  }
  return item;
}

bool UnionFind::unite(std::size_t lhs, std::size_t rhs) {
  std::size_t lhs_root = find(lhs);
  std::size_t rhs_root = find(rhs);
  if (lhs_root == rhs_root) {
    return false;
  }
  if (rank_[lhs_root] < rank_[rhs_root]) {
    std::swap(lhs_root, rhs_root);
  }
  parent_[rhs_root] = lhs_root;
  if (rank_[lhs_root] == rank_[rhs_root]) {
    ++rank_[lhs_root];
  }
  return true;
}

bool UnionFind::same(std::size_t lhs, std::size_t rhs) const { return find(lhs) == find(rhs); }

}  // namespace primo::theory::equality
