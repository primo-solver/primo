#include "util/rational.hpp"

#include <cctype>
#include <functional>

namespace primo::util {

namespace {

bool is_all_digits(std::string_view text) {
  if (text.empty()) {
    return false;
  }
  for (char c : text) {
    if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
      return false;
    }
  }
  return true;
}

}  // namespace

Rational::Rational(std::int64_t value) : value_(value) {}

Rational::Rational(std::int64_t numerator, std::int64_t denominator)
    : value_(numerator, denominator) {
  value_.canonicalize();
}

Rational::Rational(mpq_class value) : value_(std::move(value)) {}

std::optional<Rational> Rational::parse(std::string_view text) {
  const std::size_t dot = text.find('.');
  if (dot == std::string_view::npos) {
    if (!is_all_digits(text)) {
      return std::nullopt;
    }
    mpz_class numerator(std::string(text), 10);
    return Rational(mpq_class(std::move(numerator)));
  }

  const std::string_view integer_part = text.substr(0, dot);
  const std::string_view fractional_part = text.substr(dot + 1);
  if (!is_all_digits(integer_part) || !is_all_digits(fractional_part)) {
    return std::nullopt;
  }

  mpz_class numerator(std::string(integer_part) + std::string(fractional_part), 10);
  mpz_class denominator(1);
  for (std::size_t i = 0; i < fractional_part.size(); ++i) {
    denominator *= 10;
  }
  mpq_class value(std::move(numerator), std::move(denominator));
  value.canonicalize();
  return Rational(std::move(value));
}

Rational Rational::operator+(const Rational& other) const {
  return Rational(value_ + other.value_);
}

Rational Rational::operator-(const Rational& other) const {
  return Rational(value_ - other.value_);
}

Rational Rational::operator*(const Rational& other) const {
  return Rational(value_ * other.value_);
}

Rational Rational::operator/(const Rational& other) const {
  return Rational(value_ / other.value_);
}

Rational Rational::operator-() const { return Rational(-value_); }

Rational& Rational::operator+=(const Rational& other) {
  value_ += other.value_;
  return *this;
}

Rational& Rational::operator-=(const Rational& other) {
  value_ -= other.value_;
  return *this;
}

Rational& Rational::operator*=(const Rational& other) {
  value_ *= other.value_;
  return *this;
}

Rational& Rational::operator/=(const Rational& other) {
  value_ /= other.value_;
  return *this;
}

bool Rational::operator==(const Rational& other) const { return value_ == other.value_; }
bool Rational::operator!=(const Rational& other) const { return value_ != other.value_; }
bool Rational::operator<(const Rational& other) const { return value_ < other.value_; }
bool Rational::operator<=(const Rational& other) const { return value_ <= other.value_; }
bool Rational::operator>(const Rational& other) const { return value_ > other.value_; }
bool Rational::operator>=(const Rational& other) const { return value_ >= other.value_; }

bool Rational::is_zero() const { return sgn(value_) == 0; }

int Rational::sign() const { return sgn(value_); }

std::string Rational::to_string() const { return value_.get_str(); }

std::size_t RationalHash::operator()(const Rational& value) const {
  return std::hash<std::string>{}(value.value_.get_str());
}

}  // namespace primo::util
