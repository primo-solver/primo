#pragma once

#include <cstddef>
#include <vector>

namespace primo::theory::equality {

class UnionFind {
 public:
  explicit UnionFind(std::size_t size = 0);

  void reset(std::size_t size);
  std::size_t size() const;
  std::size_t find(std::size_t item);
  std::size_t find(std::size_t item) const;
  bool unite(std::size_t lhs, std::size_t rhs);
  bool same(std::size_t lhs, std::size_t rhs) const;

 private:
  std::vector<std::size_t> parent_;
  std::vector<unsigned> rank_;
};

}  // namespace primo::theory::equality
