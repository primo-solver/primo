# Word-sized GCD: what the literature says

Written 2026-08-21, after `gcd_u64` turned up as a hot spot in
[](../qf-lia-profile.md). The first pass at fixing it went straight to GMP's
source without checking the literature, which was the wrong order: the central
pathology is documented, and had been for over a decade.

## The pathology we rediscovered

Daniel Lemire's 2013 benchmark states it plainly:

> There are cases where the naive GCD algorithm is faster. If one of the two
> integers is 1, then the naive algorithm returns immediately whereas the
> binary algorithm might require many steps. Hybrid strategies may optimize for
> edge cases where one operand is trivial.

That is precisely what this solver hits. A binary GCD makes progress by
subtract-and-shift and never divides, so `gcd(a, 1)` costs O(log a) iterations
instead of one step — and `Rational::operator*` calls
`gcd(|numerator|, denominator)` on a scale-canonicalized tableau where the
denominator is very often exactly 1. Measured here at 15.9 ns (16-bit operand)
and 40.6 ns (63-bit) to compute the constant 1.

Lemire's post also confirms the other half: `__builtin_ctz` is what makes the
binary form competitive at all (17 M ops/s without it, 39 M with), and small
operands favour binary otherwise.

## The standard practical form

Wikipedia's summary matches what GMP implements and is a fair statement of the
settled practical art for machine words:

- **branch-free except the loop exit**, because "unpredictable branches can have
  a large, negative impact on performance";
- **count-trailing-zeros plus one shift**, rather than repeated division by two;
- **iterative, not recursive**, maintaining the invariant that both operands are
  odd on entry to the loop;
- **factor out the common power of two once at the start**, then only the
  odd-odd identities are needed inside.

GMP 6.3.0 (`mpn/generic/gcd_1.c`, `gcd_11.c`, both in `contrib/src`) adds two
things on top, and both are now in `gcd_u64`:

- the inner loop derives min/abs from a mask built off the sign of `u - v`,
  eliminating the `if (a > b) swap` that would otherwise mispredict about half
  the time on unrelated operands;
- the Euclidean step is **conditional**, taken only when the operands differ by
  more than 2^16 — "if u is much bigger than v, reduce using a division rather
  than chipping away at it bit-by-bit".

Reported figures in the wild are consistent with what was measured here: a basic
binary GCD around 7.7 M ops/s against 7.2 M for `std::gcd`, with hybrid
optimizations reaching 12 M. Our own change measured 1.8x on random 16-bit pairs
and 2.1x on random 63-bit pairs.

## What the actual record-holder is, and why it does not apply

Bernstein and Yang's *safegcd* (2019) and Pornin's follow-up work hold the
speed record for GCD-adjacent computation on modern 64-bit x86. Both are aimed
at **constant-time modular inversion** for cryptography, which is a different
objective from ours:

- Their advantage comes from batching *divsteps*: safegcd runs 62 iterations at
  a time against the bottom bits only and touches the full-width operands just
  12 times, so although it performs *more* total iterations (744 vs 512 for a
  256-bit inversion), each is far cheaper.
- That trade pays off for wide, multi-limb operands. For a **single 64-bit word**
  it is the wrong shape: a fixed iteration schedule cannot exit early, and this
  solver's operands are overwhelmingly tiny.
- Constant time is a feature we do not want here. It exists to defeat timing
  side channels, and it forbids exactly the input-dependent early exits that
  give us the largest win.

So the record-holder solves a problem we do not have. There is no faster
general-purpose word GCD to adopt; `std::gcd` was benchmarked too and is not
faster than the plain textbook loop.

## What the literature could not tell us

The operand *distribution*. Instrumenting the solver showed 98.3% of numerators
in the both-integer multiply case are 4 bits or fewer, with the denominator
equal to 1 — see [](../qf-lia-profile.md). At that size no asymptotic property
matters; the early exit is the whole game, and the algorithm choice underneath
it is nearly irrelevant. That is a measurement, not something a paper about
GCD could have supplied.

The general lesson for this project, since it has now cost time twice: check
whether a "basic problem" has a written answer *before* optimizing from a
profile. Here the profile told us where the time went, but the literature would
have told us why, and faster.

## Sources

- [Fastest way to compute the greatest common divisor — Daniel Lemire (2013)](https://lemire.me/blog/2013/12/26/fastest-way-to-compute-the-greatest-common-divisor/)
- [Binary GCD algorithm — Wikipedia](https://en.wikipedia.org/wiki/Binary_GCD_algorithm)
- [Fast constant-time gcd computation and modular inversion — Bernstein & Yang (2019)](https://gcd.cr.yp.to/safegcd-20190413.pdf)
- [Optimized Binary GCD for Modular Inversion — Pornin (2020)](https://eprint.iacr.org/2020/972.pdf)
- GMP 6.3.0, `mpn/generic/gcd_1.c` and `mpn/generic/gcd_11.c` (vendored in `contrib/src`)
