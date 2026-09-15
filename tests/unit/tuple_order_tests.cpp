#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstddef>
#include <set>
#include <span>
#include <vector>

#include "theory/quantifiers/tuple_orders.hpp"

using primo::theory::IndexTuple;
using primo::theory::is_staged;
using primo::theory::IterativeDeepening;
using primo::theory::RandomWalk;
using primo::theory::stage_count;
using primo::theory::TupleOrder;
using primo::theory::visit_stage;

namespace {

using Sequence = std::vector<IndexTuple>;

const TupleOrder kStaged[] = {TupleOrder::MaxDigit, TupleOrder::MaxDigitColex,
                              TupleOrder::SumOfDigits, TupleOrder::Leximax};

// Mirrors QuantifierComponent::enumerate for staged orders: resume at the
// first open stage, skip tuples already produced, close a stage only when it
// was not clipped.
struct StagedDriver {
  explicit StagedDriver(TupleOrder chosen) : order(chosen) {}

  TupleOrder order;
  std::size_t first_open = 0;
  std::set<IndexTuple> produced;

  Sequence run(std::span<const std::size_t> sizes) {
    Sequence fresh;
    const std::size_t stages = stage_count(order, sizes);
    for (std::size_t stage = first_open; stage < stages; ++stage) {
      bool clipped = false;
      const bool finished = visit_stage(
          order, stage, sizes,
          [&](const IndexTuple& tuple) {
            if (produced.insert(tuple).second) {
              fresh.push_back(tuple);
            }
            return true;
          },
          clipped);
      assert(finished);
      if (!clipped && stage == first_open) {
        ++first_open;
      }
    }
    return fresh;
  }
};

template <typename Walk>
Sequence run_walk(Walk& walk, std::span<const std::size_t> sizes, std::set<IndexTuple>& produced,
                  std::size_t limit = 100000) {
  Sequence fresh;
  IndexTuple tuple;
  for (std::size_t step = 0; step < limit && walk.next(sizes, tuple); ++step) {
    if (produced.insert(tuple).second) {
      fresh.push_back(tuple);
    }
  }
  return fresh;
}

std::size_t product(std::span<const std::size_t> sizes) {
  std::size_t count = 1;
  for (const std::size_t size : sizes) {
    count *= size;
  }
  return count;
}

void check_complete(const Sequence& sequence, std::span<const std::size_t> sizes) {
  std::set<IndexTuple> distinct(sequence.begin(), sequence.end());
  assert(distinct.size() == sequence.size());
  assert(sequence.size() == product(sizes));
  for (const IndexTuple& tuple : sequence) {
    assert(tuple.size() == sizes.size());
    for (std::size_t position = 0; position < tuple.size(); ++position) {
      assert(tuple[position] < sizes[position]);
    }
  }
}

// No tuple appears before a tuple that dominates it; checking the immediate
// predecessors (one index lowered by one) suffices by transitivity.
void check_pareto(const Sequence& sequence) {
  std::set<IndexTuple> earlier;
  for (const IndexTuple& tuple : sequence) {
    for (std::size_t position = 0; position < tuple.size(); ++position) {
      if (tuple[position] > 0) {
        IndexTuple predecessor = tuple;
        --predecessor[position];
        assert(earlier.contains(predecessor));
      }
    }
    earlier.insert(tuple);
  }
}

Sequence staged(TupleOrder order, std::vector<std::size_t> sizes) {
  StagedDriver driver(order);
  return driver.run(sizes);
}

void staged_orders_match_documented_sequences() {
  const std::vector<std::size_t> square{3, 3};
  assert((staged(TupleOrder::MaxDigit, square) ==
          Sequence{{0, 0}, {1, 0}, {1, 1}, {0, 1}, {2, 0}, {2, 1}, {2, 2}, {0, 2}, {1, 2}}));
  // Janota, Barbosa, Fontaine, Reynolds (FMCAD 2021), Section III-A.
  assert((staged(TupleOrder::MaxDigitColex, square) ==
          Sequence{{0, 0}, {1, 0}, {0, 1}, {1, 1}, {2, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2}}));
  // Section III-B.
  assert((staged(TupleOrder::SumOfDigits, square) ==
          Sequence{{0, 0}, {1, 0}, {0, 1}, {2, 0}, {1, 1}, {0, 2}, {2, 1}, {1, 2}, {2, 2}}));
  // Section III-C.
  assert((staged(TupleOrder::Leximax, square) ==
          Sequence{{0, 0}, {0, 1}, {1, 0}, {1, 1}, {0, 2}, {2, 0}, {1, 2}, {2, 1}, {2, 2}}));
  // Section III-B's three-variable prefix.
  const Sequence sum3 = staged(TupleOrder::SumOfDigits, {3, 3, 3});
  assert((Sequence(sum3.begin(), sum3.begin() + 7) ==
          Sequence{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {2, 0, 0}, {1, 1, 0}, {1, 0, 1}}));
}

void staged_orders_cover_every_tuple_once() {
  const std::vector<std::vector<std::size_t>> shapes{{1},       {5},       {3, 1, 4},   {2, 5},
                                                     {4, 4, 2}, {1, 1, 1}, {3, 2, 2, 3}};
  for (const TupleOrder order : kStaged) {
    assert(is_staged(order));
    for (const auto& sizes : shapes) {
      const Sequence sequence = staged(order, sizes);
      check_complete(sequence, sizes);
      if (order != TupleOrder::MaxDigit) {
        check_pareto(sequence);
      }
    }
  }
  // The original order is fair but not Pareto-respecting within a stage.
  const Sequence original = staged(TupleOrder::MaxDigit, {3, 3});
  assert(original[2] == (IndexTuple{1, 1}) && original[3] == (IndexTuple{0, 1}));
}

void staged_orders_resume_after_pools_grow() {
  for (const TupleOrder order : kStaged) {
    StagedDriver driver(order);
    Sequence all = driver.run(std::vector<std::size_t>{2, 1, 2});
    for (const auto& sizes :
         std::vector<std::vector<std::size_t>>{{2, 3, 2}, {4, 3, 2}, {4, 3, 5}}) {
      const Sequence fresh = driver.run(sizes);
      all.insert(all.end(), fresh.begin(), fresh.end());
    }
    check_complete(all, std::vector<std::size_t>{4, 3, 5});
    // A second run over unchanged pools adds nothing.
    assert(driver.run(std::vector<std::size_t>{4, 3, 5}).empty());
  }
}

void iterative_deepening_matches_the_paper_and_grows() {
  // Section III-D: maximum depth 2 on two variables.
  IterativeDeepening walk(2);
  std::set<IndexTuple> produced;
  const std::vector<std::size_t> square{3, 3};
  const Sequence sequence = run_walk(walk, square, produced);
  assert((Sequence(sequence.begin(), sequence.begin() + 6) ==
          Sequence{{0, 0}, {0, 1}, {0, 2}, {1, 1}, {1, 0}, {2, 0}}));
  assert((Sequence(sequence.begin() + 6, sequence.end()) == Sequence{{1, 2}, {2, 2}, {2, 1}}));
  check_complete(sequence, square);

  // Resumption: exhaust small pools, then grow them.
  IterativeDeepening growing(1);
  std::set<IndexTuple> seen;
  Sequence all = run_walk(growing, std::vector<std::size_t>{2, 2, 1}, seen);
  const Sequence more = run_walk(growing, std::vector<std::size_t>{3, 4, 2}, seen);
  all.insert(all.end(), more.begin(), more.end());
  check_complete(all, std::vector<std::size_t>{3, 4, 2});
  assert(run_walk(growing, std::vector<std::size_t>{3, 4, 2}, seen).empty());
}

void random_walk_is_reproducible_and_complete() {
  const std::vector<std::size_t> sizes{4, 3, 5};
  std::set<IndexTuple> first_seen;
  std::set<IndexTuple> second_seen;
  std::set<IndexTuple> other_seen;
  RandomWalk first(7);
  RandomWalk second(7);
  RandomWalk other(8);
  const Sequence a = run_walk(first, sizes, first_seen);
  const Sequence b = run_walk(second, sizes, second_seen);
  const Sequence c = run_walk(other, sizes, other_seen);
  assert(a == b);
  assert(a != c);
  check_complete(a, sizes);
  check_complete(c, sizes);
  assert(a.front() == (IndexTuple{0, 0, 0}));

  RandomWalk growing(3);
  std::set<IndexTuple> seen;
  Sequence all = run_walk(growing, std::vector<std::size_t>{1, 2, 1}, seen);
  const Sequence more = run_walk(growing, sizes, seen);
  all.insert(all.end(), more.begin(), more.end());
  check_complete(all, sizes);
}

}  // namespace

int main() {
  staged_orders_match_documented_sequences();
  staged_orders_cover_every_tuple_once();
  staged_orders_resume_after_pools_grow();
  iterative_deepening_matches_the_paper_and_grows();
  random_walk_is_reproducible_and_complete();
  return 0;
}
