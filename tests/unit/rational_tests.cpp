#ifdef NDEBUG
#undef NDEBUG
#endif

#include <gmpxx.h>

#include <cassert>
#include <cstdint>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "util/rational.hpp"

using primo::util::Rational;
using primo::util::RationalHash;

namespace {

void construction_and_equality() {
  assert(Rational(3) == Rational(3));
  assert(Rational(3) != Rational(4));
  assert(Rational(6, 4) == Rational(3, 2));
  assert(Rational(-6, 4) == Rational(-3, 2));
  assert(Rational(6, -4) == Rational(-3, 2));
}

void arithmetic() {
  assert(Rational(1, 2) + Rational(1, 3) == Rational(5, 6));
  assert(Rational(1, 2) - Rational(1, 3) == Rational(1, 6));
  assert(Rational(2, 3) * Rational(3, 4) == Rational(1, 2));
  assert(Rational(2, 3) / Rational(4, 9) == Rational(3, 2));
  assert(-Rational(2, 3) == Rational(-2, 3));

  Rational accumulator(0);
  accumulator += Rational(1, 2);
  accumulator += Rational(1, 3);
  accumulator += Rational(1, 6);
  assert(accumulator == Rational(1));
}

void ordering() {
  assert(Rational(1, 3) < Rational(1, 2));
  assert(Rational(1, 2) <= Rational(1, 2));
  assert(Rational(2, 3) > Rational(1, 2));
  assert(Rational(1, 2) >= Rational(1, 2));
  assert(!(Rational(1, 2) < Rational(1, 2)));
}

void zero_and_sign() {
  assert(Rational(0).is_zero());
  assert(Rational(0, 5).is_zero());
  assert(Rational(3).sign() == 1);
  assert(Rational(-3).sign() == -1);
  assert(Rational(0).sign() == 0);
}

void parse_numerals() {
  const auto zero = Rational::parse("0");
  assert(zero.has_value());
  assert(*zero == Rational(0));

  const auto three = Rational::parse("3");
  assert(three.has_value());
  assert(*three == Rational(3));

  const auto big = Rational::parse("123456789012345678901234567890");
  assert(big.has_value());
  // Exceeds int64_t range; this would fail with a fixed-width backing type.
  assert(big->to_string() == "123456789012345678901234567890");
}

void parse_decimals() {
  const auto half = Rational::parse("0.5");
  assert(half.has_value());
  assert(*half == Rational(1, 2));

  const auto one_five = Rational::parse("1.5");
  assert(one_five.has_value());
  assert(*one_five == Rational(3, 2));

  const auto trailing_zeros = Rational::parse("2.500");
  assert(trailing_zeros.has_value());
  assert(*trailing_zeros == Rational(5, 2));
}

void parse_rejects_malformed_input() {
  assert(!Rational::parse("").has_value());
  assert(!Rational::parse(".").has_value());
  assert(!Rational::parse(".5").has_value());
  assert(!Rational::parse("3.").has_value());
  assert(!Rational::parse("-3").has_value());  // Sign handling belongs to the caller.
  assert(!Rational::parse("3.5.5").has_value());
  assert(!Rational::parse("abc").has_value());
}

void to_string_round_trips() {
  assert(Rational(3).to_string() == "3");
  assert(Rational(-3).to_string() == "-3");
  assert(Rational(1, 2).to_string() == "1/2");
  assert(Rational(-1, 2).to_string() == "-1/2");
}

// -- Two-tier representation (docs/qf-lra-heuristics.md, H2) --
//
// Rational stores small values inline as int64/int64 and only falls back to a
// heap mpq_class when a value does not fit, with the invariant that the big
// form is used *if and only if* the small form cannot hold the value. The
// tests below exercise that switch directly, because getting it wrong is
// mostly silent: a missed demotion computes the right number but hashes it
// differently, which would quietly break polynomial hash-consing rather than
// produce a visibly wrong answer.

// A Rational paired with an independently built GMP reference for the same
// value, so every operation can be differentially checked.
struct Checked {
  Rational value;
  mpq_class reference;
};

mpq_class reference_of(std::int64_t numerator, std::int64_t denominator) {
  mpq_class result(mpz_class(static_cast<long>(numerator)),     // NOLINT(google-runtime-int)
                   mpz_class(static_cast<long>(denominator)));  // NOLINT(google-runtime-int)
  result.canonicalize();
  return result;
}

Checked checked(std::int64_t numerator, std::int64_t denominator) {
  return Checked{Rational(numerator, denominator), reference_of(numerator, denominator)};
}

// Builds a value from decimal digit strings, so the reference side never goes
// through the code under test. Used for the values too large for the small
// form.
Checked checked_big(const std::string& numerator, const std::string& denominator, bool negative) {
  const auto parsed_numerator = Rational::parse(numerator);
  const auto parsed_denominator = Rational::parse(denominator);
  assert(parsed_numerator.has_value() && parsed_denominator.has_value());
  Rational value = *parsed_numerator / *parsed_denominator;
  if (negative) {
    value = -value;
  }
  mpq_class reference(mpz_class(numerator, 10), mpz_class(denominator, 10));
  reference.canonicalize();
  if (negative) {
    reference = -reference;
  }
  return Checked{value, reference};
}

void check_matches(const Rational& value, const mpq_class& reference, const char* what) {
  (void)what;
  assert(value.to_string() == reference.get_str());
  assert(value.sign() == sgn(reference));
  assert(value.is_zero() == (sgn(reference) == 0));
}

std::vector<Checked> sample_values() {
  constexpr std::int64_t kMax = INT64_MAX;
  std::vector<Checked> values;

  // Small, boundary, and near-overflow inputs.
  const std::int64_t interesting[] = {0,          1,    -1,    2,        -2,         3,
                                      7,          -7,   1000,  -1000,    2147483647, -2147483647,
                                      4294967296, kMax, -kMax, kMax - 1, 3037000499};
  for (const std::int64_t numerator : interesting) {
    values.push_back(checked(numerator, 1));
    values.push_back(checked(numerator, 3));
    values.push_back(checked(numerator, kMax));
  }

  // Deterministic random pairs across magnitude buckets.
  std::mt19937_64 rng(20260728);
  const std::int64_t buckets[] = {1000, 1LL << 31, 1LL << 62, kMax};
  for (const std::int64_t bound : buckets) {
    for (int i = 0; i < 12; ++i) {
      const auto pick = [&]() {
        const std::int64_t magnitude =
            static_cast<std::int64_t>(rng() % static_cast<std::uint64_t>(bound)) + 1;
        return (rng() % 2) == 0 ? magnitude : -magnitude;
      };
      const std::int64_t denominator = pick();
      values.push_back(checked(pick(), denominator == 0 ? 1 : denominator));
    }
  }

  // Values that only the big form can hold, so the mixed and big/big paths are
  // covered too.
  values.push_back(checked_big("123456789012345678901234567890", "1", false));
  values.push_back(checked_big("123456789012345678901234567890", "1", true));
  values.push_back(checked_big("1", "123456789012345678901234567890", false));
  values.push_back(checked_big("99999999999999999999999999999999999999", "7", false));
  values.push_back(checked_big("340282366920938463463374607431768211456", "3", true));

  return values;
}

// Differential test against mpq_class over every ordered pair of the sample
// above: small/small (including results that must promote), small/big, and
// big/big (including results that must demote back).
void differential_against_gmp() {
  const std::vector<Checked> values = sample_values();
  for (const Checked& left : values) {
    check_matches(left.value, left.reference, "construction");
    check_matches(-left.value, mpq_class(-left.reference), "negation");

    for (const Checked& right : values) {
      check_matches(left.value + right.value, mpq_class(left.reference + right.reference), "add");
      check_matches(left.value - right.value, mpq_class(left.reference - right.reference), "sub");
      check_matches(left.value * right.value, mpq_class(left.reference * right.reference), "mul");
      if (sgn(right.reference) != 0) {
        check_matches(left.value / right.value, mpq_class(left.reference / right.reference), "div");
      }

      assert((left.value < right.value) == (left.reference < right.reference));
      assert((left.value <= right.value) == (left.reference <= right.reference));
      assert((left.value > right.value) == (left.reference > right.reference));
      assert((left.value >= right.value) == (left.reference >= right.reference));
      assert((left.value == right.value) == (left.reference == right.reference));
      assert((left.value != right.value) == (left.reference != right.reference));
    }
  }
}

// One case per transition between the two representations.
void representation_transitions() {
  const Rational max(INT64_MAX);
  const Rational one(1);

  // Small * small that overflows into the big form.
  const Rational promoted = max * max;
  assert(promoted.to_string() == mpq_class(mpz_class(std::to_string(INT64_MAX), 10) *
                                           mpz_class(std::to_string(INT64_MAX), 10))
                                     .get_str());

  // ...and straight back down again: the result fits, so it must be small once
  // more, which is observable as equality plus hash equality with a value that
  // never left the small form.
  const Rational demoted = promoted / max;
  assert(demoted == max);
  assert(RationalHash{}(demoted) == RationalHash{}(max));

  // A denominator that overflows.
  const Rational tiny = Rational(1, INT64_MAX) * Rational(1, INT64_MAX);
  assert(tiny * (max * max) == one);

  // Mixed small/big operands, in both orders.
  const auto big = Rational::parse("123456789012345678901234567890");
  assert(big.has_value());
  assert((*big + Rational(0)) == *big);
  assert((Rational(0) + *big) == *big);
  assert((*big - *big).is_zero());
  assert((*big * Rational(0)).is_zero());

  // INT64_MIN does not fit the small form (its magnitude is one past the
  // largest admissible numerator), so it exercises the promoting path on
  // construction; the value must still be exact.
  const Rational int64_min(INT64_MIN);
  assert(int64_min.to_string() == "-9223372036854775808");
  assert(int64_min + one == Rational(INT64_MIN + 1));
  assert((int64_min - int64_min).is_zero());
  assert(int64_min < Rational(-INT64_MAX));
}

// Equal values must be == and hash equal no matter which path produced them --
// this is what the canonical-form invariant exists for, and what silently
// breaks polynomial hash-consing if demotion is ever dropped.
void canonical_form_is_path_independent() {
  const Rational direct(6, 4);
  const Rational via_arithmetic = Rational(1, 2) + Rational(1);
  const Rational via_big =
      (Rational(3) * Rational(INT64_MAX)) / (Rational(2) * Rational(INT64_MAX));
  assert(direct == via_arithmetic);
  assert(direct == via_big);
  assert(RationalHash{}(direct) == RationalHash{}(via_arithmetic));
  assert(RationalHash{}(direct) == RationalHash{}(via_big));

  const auto parsed = Rational::parse("1.5");
  assert(parsed.has_value());
  assert(*parsed == direct);
  assert(RationalHash{}(*parsed) == RationalHash{}(direct));

  // Same, for a value that genuinely needs the big form: reached by parsing and
  // by arithmetic.
  const auto huge = Rational::parse("123456789012345678901234567890");
  assert(huge.has_value());
  const Rational huge_by_arithmetic =
      *Rational::parse("61728394506172839450617283945") * Rational(2);
  assert(*huge == huge_by_arithmetic);
  assert(RationalHash{}(*huge) == RationalHash{}(huge_by_arithmetic));
}

void promotion_counter_reports_the_fast_path() {
  // The counter is what makes the small form's width an empirically answerable
  // question (docs/qf-lra-heuristics.md's H2 width ablation), so check it
  // actually moves -- and only when it should.
  const std::uint64_t before = Rational::promotion_count();
  for (int i = 0; i < 100; ++i) {
    const Rational sum = Rational(i, 7) + Rational(i, 3);
    assert(!sum.to_string().empty());
  }
  assert(Rational::promotion_count() == before);

  const Rational overflowing = Rational(INT64_MAX) * Rational(INT64_MAX);
  assert(!overflowing.is_zero());
  assert(Rational::promotion_count() > before);
}

// Division by zero and a zero denominator must fail loudly. mpq_class used to
// do that for free (GMP raises); the small form would instead produce a
// canonical-looking value with den_ == 0 and propagate it silently through
// every later operation, which is a far worse failure mode in the numeric
// core. Not reachable from the tableau today -- rows never hold a zero
// coefficient -- but not a thing to leave resting on that.
void division_by_zero_throws() {
  bool threw = false;
  try {
    const Rational quotient = Rational(3) / Rational(0);
    (void)quotient;
  } catch (const std::domain_error&) {
    threw = true;
  }
  assert(threw);

  threw = false;
  try {
    const Rational bad(1, 0);
    (void)bad;
  } catch (const std::domain_error&) {
    threw = true;
  }
  assert(threw);

  // The big path must agree with the small one.
  const auto huge = Rational::parse("123456789012345678901234567890");
  assert(huge.has_value());
  threw = false;
  try {
    const Rational quotient = *huge / Rational(0);
    (void)quotient;
  } catch (const std::domain_error&) {
    threw = true;
  }
  assert(threw);
}

}  // namespace

int main() {
  construction_and_equality();
  arithmetic();
  ordering();
  zero_and_sign();
  parse_numerals();
  parse_decimals();
  parse_rejects_malformed_input();
  to_string_round_trips();
  differential_against_gmp();
  representation_transitions();
  canonical_form_is_path_independent();
  promotion_counter_reports_the_fast_path();
  division_by_zero_throws();
  return 0;
}
