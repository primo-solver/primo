#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <gmpxx.h>

namespace primo::util {

// Exact rational arithmetic, backed by GMP's mpq_class. Kept as Primo's own
// type (rather than using mpq_class directly at every call site) so the GMP
// dependency stays contained to this file, the same way CadicalBackend
// contains the CaDiCaL dependency.
class Rational {
 public:
  Rational() = default;
  Rational(std::int64_t value);  // NOLINT(google-explicit-constructor)
  Rational(std::int64_t numerator, std::int64_t denominator);

  // Parses an SMT-LIB NUMERAL ("3") or DECIMAL ("1.5", "-0.25") token into
  // an exact Rational. Returns std::nullopt if `text` isn't shaped like
  // either (the caller is expected to have already decided `text` is a
  // candidate literal, e.g. by checking its first character).
  static std::optional<Rational> parse(std::string_view text);

  Rational operator+(const Rational& other) const;
  Rational operator-(const Rational& other) const;
  Rational operator*(const Rational& other) const;
  Rational operator/(const Rational& other) const;
  Rational operator-() const;

  Rational& operator+=(const Rational& other);
  Rational& operator-=(const Rational& other);
  Rational& operator*=(const Rational& other);
  Rational& operator/=(const Rational& other);

  bool operator==(const Rational& other) const;
  bool operator!=(const Rational& other) const;
  bool operator<(const Rational& other) const;
  bool operator<=(const Rational& other) const;
  bool operator>(const Rational& other) const;
  bool operator>=(const Rational& other) const;

  bool is_zero() const;
  int sign() const;  // -1, 0, or 1.

  std::string to_string() const;

 private:
  explicit Rational(mpq_class value);

  mpq_class value_{};

  friend struct RationalHash;
};

// Hashes the canonical (numerator, denominator) pair so equal Rationals
// (post-mpq_class::canonicalize) always hash equal, for use as a
// LinearPolynomial coefficient / hash-consing key component.
struct RationalHash {
  std::size_t operator()(const Rational& value) const;
};

}  // namespace primo::util
