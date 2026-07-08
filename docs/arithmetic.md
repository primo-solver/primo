# Arithmetic

Arithmetic is a future target. The initial scaffold does not implement full Simplex or difference logic.

## Future LRA data structures

```text
LinearPolynomial:
  coeffs: Map[Var, Rational]
  constant: Rational

Bound:
  var
  kind: <= | >= | < | >
  value: Rational
  source_lit

TableauRow:
  basic_var
  terms: Map[nonbasic_var, Rational]

ArithmeticTrailEntry:
  bound update
  previous bound
  source literal
  explanation
```

## Initial TODOs

- represent rational coefficients
- represent linear polynomials
- normalize arithmetic atoms
- define bounds
- define tableau rows
- add parser/type-checker tests later
