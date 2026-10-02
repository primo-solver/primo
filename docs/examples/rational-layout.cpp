// Layout probe only. Candidate records are never constructed or used for
// arithmetic; working unions need explicit active-member lifetime handling.
#include <cstdint>
#include <iostream>
#include <memory>

#include "theory/arithmetic/lra_tableau.hpp"
#include "util/rational.hpp"

using primo::theory::arithmetic::DeltaRational;
using primo::theory::arithmetic::LraTableau;
using primo::theory::arithmetic::LraVarId;
using primo::util::Rational;

struct SmallPair {
  std::int64_t numerator;
  std::int64_t denominator;
};

struct RawPointerLayout {
  std::int64_t numerator;
  std::int64_t denominator;
  mpq_class* big;
};

union SmallOrPointer {
  SmallPair small;
  std::unique_ptr<mpq_class> big;
};

struct SeparateTagLayout {
  bool is_big;
  SmallOrPointer payload;
};

union NumeratorOrPointer {
  std::int64_t numerator;
  std::unique_ptr<mpq_class> big;
};

struct DenominatorTagLayout {
  std::int64_t denominator;  // Positive for small form; zero would select big.
  NumeratorOrPointer payload;
};

union SmallOrInlineGmp {
  SmallPair small;
  mpq_class big;
};

struct InlineGmpLayout {
  bool is_big;
  SmallOrInlineGmp payload;
};

struct CompactDeltaLayout {
  DenominatorTagLayout rational;
  DenominatorTagLayout infinitesimal;
};

struct CompactRowEntryLayout {
  LraVarId var;
  DenominatorTagLayout coeff;
  std::uint32_t occ_slot;
};

int main() {
  std::cout << "Raw GMP pointer: " << sizeof(mpq_class*) << '\n'
            << "unique_ptr with default deleter: " << sizeof(std::unique_ptr<mpq_class>) << '\n'
            << "GMP rational object: " << sizeof(mpq_class) << '\n'
            << "Current Rational: " << sizeof(Rational) << '\n'
            << "Same fields with raw pointer: " << sizeof(RawPointerLayout) << '\n'
            << "Union with separate tag: " << sizeof(SeparateTagLayout) << '\n'
            << "Union with denominator tag: " << sizeof(DenominatorTagLayout) << '\n'
            << "Union with inline GMP and separate tag: " << sizeof(InlineGmpLayout) << '\n'
            << "Current DeltaRational: " << sizeof(DeltaRational) << '\n'
            << "Candidate compact delta: " << sizeof(CompactDeltaLayout) << '\n'
            << "Current RowEntry: " << sizeof(LraTableau::RowEntry) << '\n'
            << "Candidate compact row entry: " << sizeof(CompactRowEntryLayout) << '\n';
}
