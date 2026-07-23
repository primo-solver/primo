#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <optional>

#include "util/rational.hpp"

using primo::util::Rational;

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
  return 0;
}
