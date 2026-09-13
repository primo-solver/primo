#include "theory/quantifiers/tuple_orders.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace primo::theory {

namespace {

bool has_empty_pool(std::span<const std::size_t> sizes) {
  return std::any_of(sizes.begin(), sizes.end(), [](std::size_t size) { return size == 0; });
}

bool within(std::span<const std::size_t> sizes, const IndexTuple& tuple) {
  for (std::size_t position = 0; position < tuple.size(); ++position) {
    if (tuple[position] >= sizes[position]) {
      return false;
    }
  }
  return true;
}

// The original order: tuples whose largest index is `stage`, grouped by the
// first position holding it; earlier positions range below the stage, later
// ones up to it, and the rightmost free position varies fastest.
bool visit_max_digit(std::size_t stage, std::span<const std::size_t> sizes,
                     const TupleVisitor& visit) {
  const std::size_t arity = sizes.size();
  IndexTuple tuple(arity);
  for (std::size_t first = 0; first < arity; ++first) {
    if (sizes[first] <= stage) {
      continue;
    }
    std::vector<std::size_t> bound(arity);
    bool empty = false;
    for (std::size_t position = 0; position < arity; ++position) {
      bound[position] = position < first    ? std::min(stage, sizes[position])
                        : position == first ? 1
                                            : std::min(stage + 1, sizes[position]);
      empty = empty || bound[position] == 0;
    }
    if (empty) {
      continue;
    }
    std::vector<std::size_t> index(arity, 0);
    while (true) {
      for (std::size_t position = 0; position < arity; ++position) {
        tuple[position] = position == first ? stage : index[position];
      }
      if (!visit(tuple)) {
        return false;
      }
      bool done = true;
      for (std::size_t position = arity; position-- > 0;) {
        if (position == first) {
          continue;
        }
        if (++index[position] < bound[position]) {
          done = false;
          break;
        }
        index[position] = 0;
      }
      if (done) {
        break;
      }
    }
  }
  return true;
}

// Tuples whose largest index is `stage`, colexicographically: the last position
// is the most significant. A position is pinned to the stage value when no
// more significant position holds it and no less significant one can, so every
// partial tuple extends to a member of the stage.
bool visit_max_digit_colex(std::size_t stage, std::span<const std::size_t> sizes,
                           const TupleVisitor& visit) {
  const std::size_t arity = sizes.size();
  // can_lower[i]: some position below i can hold the stage value.
  std::vector<bool> can_lower(arity, false);
  for (std::size_t position = 1; position < arity; ++position) {
    can_lower[position] = can_lower[position - 1] || sizes[position - 1] > stage;
  }
  IndexTuple digit(arity, 0);
  const auto pinned = [&](std::size_t position) {
    if (can_lower[position]) {
      return false;
    }
    for (std::size_t above = position + 1; above < arity; ++above) {
      if (digit[above] == stage) {
        return false;
      }
    }
    return true;
  };
  // Smallest completion of the positions below `top`.
  const auto complete_below = [&](std::size_t top) {
    for (std::size_t position = top; position-- > 0;) {
      if (pinned(position)) {
        if (sizes[position] <= stage) {
          return false;
        }
        digit[position] = stage;
      } else {
        digit[position] = 0;
      }
    }
    return true;
  };
  if (!complete_below(arity)) {
    return true;  // no position can hold the stage value
  }
  while (true) {
    if (!visit(digit)) {
      return false;
    }
    std::size_t position = 0;
    for (; position < arity; ++position) {
      if (!pinned(position) && digit[position] < std::min(stage, sizes[position] - 1)) {
        break;
      }
    }
    if (position == arity) {
      return true;
    }
    ++digit[position];
    // A completion exists: either the stage value sits at or above
    // `position`, or a lower position can take it.
    static_cast<void>(complete_below(position));
  }
}

// Tuples whose indices sum to `stage`, in descending lexicographic order.
bool visit_sum(std::size_t stage, std::span<const std::size_t> sizes, const TupleVisitor& visit) {
  const std::size_t arity = sizes.size();
  // capacity[i]: the largest sum positions i.. can hold.
  std::vector<std::size_t> capacity(arity + 1, 0);
  for (std::size_t position = arity; position-- > 0;) {
    capacity[position] = capacity[position + 1] + (sizes[position] - 1);
  }
  if (stage > capacity[0]) {
    return true;
  }
  IndexTuple digit(arity, 0);
  // Largest suffix from `from` with the given sum.
  const auto fill = [&](std::size_t from, std::size_t amount) {
    for (std::size_t position = from; position < arity; ++position) {
      const std::size_t take = std::min(amount, sizes[position] - 1);
      digit[position] = take;
      amount -= take;
    }
  };
  fill(0, stage);
  while (true) {
    if (!visit(digit)) {
      return false;
    }
    bool advanced = false;
    for (std::size_t position = arity - 1; position-- > 0;) {
      if (digit[position] == 0) {
        continue;
      }
      std::size_t rest = 1;
      for (std::size_t later = position + 1; later < arity; ++later) {
        rest += digit[later];
      }
      if (rest <= capacity[position + 1]) {
        --digit[position];
        fill(position + 1, rest);
        advanced = true;
        break;
      }
    }
    if (!advanced) {
      return true;
    }
  }
}

// Tuples whose largest index is `stage` in leximax order: descending sorted
// tuples headed by the stage value, lexicographically, each followed by its
// distinct permutations in lexicographic order.
bool visit_leximax(std::size_t stage, std::span<const std::size_t> sizes,
                   const TupleVisitor& visit) {
  const std::size_t arity = sizes.size();
  if (*std::max_element(sizes.begin(), sizes.end()) <= stage) {
    return true;
  }
  IndexTuple sorted(arity, 0);
  sorted[0] = stage;
  while (true) {
    IndexTuple permutation(sorted.rbegin(), sorted.rend());
    do {
      if (within(sizes, permutation) && !visit(permutation)) {
        return false;
      }
    } while (std::next_permutation(permutation.begin(), permutation.end()));
    std::size_t position = arity;
    bool found = false;
    while (position > 1) {
      --position;
      if (sorted[position] < sorted[position - 1]) {
        found = true;
        break;
      }
    }
    if (!found) {
      return true;
    }
    ++sorted[position];
    std::fill(sorted.begin() + static_cast<std::ptrdiff_t>(position) + 1, sorted.end(), 0);
  }
}

}  // namespace

bool is_staged(TupleOrder order) {
  return order != TupleOrder::IterativeDeepening && order != TupleOrder::RandomWalk;
}

std::size_t stage_count(TupleOrder order, std::span<const std::size_t> sizes) {
  if (sizes.empty() || has_empty_pool(sizes)) {
    return 0;
  }
  if (order == TupleOrder::SumOfDigits) {
    std::size_t total = 0;
    for (const std::size_t size : sizes) {
      total += size - 1;
    }
    return total + 1;
  }
  return *std::max_element(sizes.begin(), sizes.end());
}

bool visit_stage(TupleOrder order, std::size_t stage, std::span<const std::size_t> sizes,
                 const TupleVisitor& visit, bool& clipped) {
  // A tuple outside the pools exists exactly when some position can take the
  // stage value only beyond its pool (for sums: the whole sum in one place).
  clipped =
      std::any_of(sizes.begin(), sizes.end(), [stage](std::size_t size) { return size <= stage; });
  if (sizes.empty() || has_empty_pool(sizes)) {
    return true;
  }
  switch (order) {
    case TupleOrder::MaxDigit:
      return visit_max_digit(stage, sizes, visit);
    case TupleOrder::MaxDigitColex:
      return visit_max_digit_colex(stage, sizes, visit);
    case TupleOrder::SumOfDigits:
      return visit_sum(stage, sizes, visit);
    case TupleOrder::Leximax:
      return visit_leximax(stage, sizes, visit);
    case TupleOrder::IterativeDeepening:
    case TupleOrder::RandomWalk:
      break;
  }
  throw std::invalid_argument("visit_stage needs a staged tuple order");
}

IterativeDeepening::IterativeDeepening(std::size_t increment) : increment_(increment) {
  if (increment_ == 0) {
    throw std::invalid_argument("iterative deepening needs a positive increment");
  }
}

bool IterativeDeepening::next(std::span<const std::size_t> sizes, IndexTuple& tuple) {
  const std::size_t arity = sizes.size();
  if (arity == 0 || has_empty_pool(sizes)) {
    return false;
  }
  while (true) {
    if (stack_.empty()) {
      const bool grew = started_ && !std::equal(sizes.begin(), sizes.end(), sizes_at_start_.begin(),
                                                sizes_at_start_.end());
      if (started_ && !depth_limited_ && !grew) {
        return false;
      }
      if (!started_) {
        max_depth_ = increment_;
        started_ = true;
      } else if (depth_limited_) {
        max_depth_ += increment_;
      }
      depth_limited_ = false;
      sizes_at_start_.assign(sizes.begin(), sizes.end());
      visited_.clear();
      IndexTuple root(arity, 0);
      visited_.insert(root);
      stack_.push_back({root, 0, arity});
      tuple = std::move(root);
      return true;
    }
    Frame& frame = stack_.back();
    while (frame.next_position > 0) {
      const std::size_t position = --frame.next_position;
      IndexTuple child = frame.tuple;
      ++child[position];
      if (child[position] >= sizes[position]) {
        continue;
      }
      if (frame.depth + 1 > max_depth_) {
        depth_limited_ = true;
        continue;
      }
      if (!visited_.insert(child).second) {
        continue;
      }
      const std::size_t depth = frame.depth + 1;
      tuple = child;
      stack_.push_back({std::move(child), depth, arity});  // invalidates `frame`
      return true;
    }
    stack_.pop_back();
  }
}

RandomWalk::RandomWalk(std::uint64_t seed) : rng_(seed) {}

bool RandomWalk::next(std::span<const std::size_t> sizes, IndexTuple& tuple) {
  const std::size_t arity = sizes.size();
  if (arity == 0 || has_empty_pool(sizes)) {
    return false;
  }
  if (!started_) {
    started_ = true;
    IndexTuple root(arity, 0);
    seen_.insert(root);
    frontier_.push_back(std::move(root));
  }
  if (frontier_.empty()) {
    // The pools may have grown: successors that were outside them can move in.
    std::vector<IndexTuple> still_blocked;
    for (IndexTuple& candidate : blocked_) {
      if (within(sizes, candidate)) {
        frontier_.push_back(std::move(candidate));
      } else {
        still_blocked.push_back(std::move(candidate));
      }
    }
    blocked_ = std::move(still_blocked);
    if (frontier_.empty()) {
      return false;
    }
  }
  std::uniform_int_distribution<std::size_t> pick(0, frontier_.size() - 1);
  const std::size_t chosen = pick(rng_);
  std::swap(frontier_[chosen], frontier_.back());
  tuple = std::move(frontier_.back());
  frontier_.pop_back();
  for (std::size_t position = 0; position < arity; ++position) {
    IndexTuple successor = tuple;
    ++successor[position];
    if (!seen_.insert(successor).second) {
      continue;
    }
    if (within(sizes, successor)) {
      frontier_.push_back(std::move(successor));
    } else {
      blocked_.push_back(std::move(successor));
    }
  }
  return true;
}

}  // namespace primo::theory
