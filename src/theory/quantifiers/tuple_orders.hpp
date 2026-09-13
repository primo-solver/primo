#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <set>
#include <span>
#include <vector>

namespace primo::theory {

// Orders in which a quantifier's variables are assigned tuples of candidate
// terms, as indices into per-variable pools (docs/papers/
// fair-adventurous-enumeration.md: Janota, Barbosa, Fontaine and Reynolds,
// FMCAD 2021, Section III). Pools only grow, so every order must eventually
// reach a tuple that becomes valid after its stage or walk has passed it.
enum class TupleOrder {
  // Stages by maximal index, grouped within a stage by the first position that
  // holds the stage value; the original order of the quantifier component. Not
  // Pareto-respecting within a stage: (1,1) comes before (0,1).
  MaxDigit,
  // Stages by maximal index, colexicographic within a stage (last position
  // most significant): the paper's order A, e.g. (0,0) (1,0) (0,1) (1,1) (2,0).
  MaxDigitColex,
  // Stages by sum of indices, descending lexicographic within a stage: the
  // paper's order B, e.g. (0,0) (1,0) (0,1) (2,0) (1,1) (0,2).
  SumOfDigits,
  // Leximax: stages by maximal index; within a stage by the tuple sorted in
  // descending order, lexicographically, each sorted tuple followed by its
  // permutations in lexicographic order: the paper's order C, e.g.
  // (0,0) (0,1) (1,0) (1,1) (0,2) (2,0).
  Leximax,
  // Depth-first search of the Pareto graph (an edge increases one index by
  // one), children by the last position first, to a maximum index sum that
  // grows by an increment: the paper's order D. Not Pareto-respecting.
  IterativeDeepening,
  // Random choice from the frontier of the Pareto graph: the paper's random
  // walk. Not Pareto-respecting; reproducible for one seed and one build.
  RandomWalk,
};

// The only definition of the default order, the best single order on the
// SMT-LIB UF division (docs/quantifiers.md).
inline constexpr TupleOrder kDefaultTupleOrder = TupleOrder::SumOfDigits;

using IndexTuple = std::vector<std::size_t>;
// Receives one tuple; returns false to stop the enumeration.
using TupleVisitor = std::function<bool(const IndexTuple&)>;

// Whether the order enumerates finite stages that visit_stage covers.
bool is_staged(TupleOrder order);

// Number of stages that can hold a tuple within `sizes` (stage values 0 to
// count - 1); zero when some pool is empty.
std::size_t stage_count(TupleOrder order, std::span<const std::size_t> sizes);

// Visits the tuples of one stage that lie within `sizes`, in the order's
// sequence. `clipped` is set when the stage has a tuple outside `sizes`, that
// is, when pool growth can add tuples to it. Returns false when the visitor
// stopped. Requires a staged order.
bool visit_stage(TupleOrder order, std::size_t stage, std::span<const std::size_t> sizes,
                 const TupleVisitor& visit, bool& clipped);

// Resumable iterative deepening. next() returns tuples in depth-first order up
// to the current maximum index sum; when a traversal ends it restarts from the
// root, with the limit raised if the traversal met it, or unchanged if the
// pools grew since it began. A restart revisits earlier tuples; callers skip
// the ones they have seen. Returns false once no traversal can add a tuple.
class IterativeDeepening {
 public:
  explicit IterativeDeepening(std::size_t increment);
  bool next(std::span<const std::size_t> sizes, IndexTuple& tuple);

 private:
  struct Frame {
    IndexTuple tuple;
    std::size_t depth = 0;
    std::size_t next_position = 0;  // positions below this are still to try
  };

  std::size_t increment_;
  std::size_t max_depth_ = 0;
  bool started_ = false;
  bool depth_limited_ = false;
  std::vector<std::size_t> sizes_at_start_;
  std::vector<Frame> stack_;
  std::set<IndexTuple> visited_;
};

// Resumable random walk: each call removes a random tuple from the frontier and
// adds its unseen successors. A successor outside the pools waits until they
// grow. Every tuple is returned at most once.
class RandomWalk {
 public:
  explicit RandomWalk(std::uint64_t seed);
  bool next(std::span<const std::size_t> sizes, IndexTuple& tuple);

 private:
  std::mt19937_64 rng_;
  bool started_ = false;
  std::vector<IndexTuple> frontier_;
  std::vector<IndexTuple> blocked_;
  std::set<IndexTuple> seen_;
};

}  // namespace primo::theory
