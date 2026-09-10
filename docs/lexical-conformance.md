# SMT-LIB lexical conformance audit

## Status and authoritative specification

The audit of `9f20f58` exposes **two wrong-answer cases**, eleven rejected valid
cases, and multiple accepted invalid inputs. These are unresolved correctness
and conformance findings, not performance work. Prior scoped fuzzing did not
exercise the quoted/unquoted name equivalence behind the wrong answers.

The source of truth is Barrett, Fontaine and Tinelli,
[SMT-LIB 2.7, release 2026-03-27](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-03-27.pdf):
Sections 3.1–3.3 specify lexical categories and identifiers; 3.6.1–3.6.2 specify
binding; 4.1.4–4.1.5 specify scoped declarations; Appendix B collects syntax.
Reference-solver acceptance is evidence about interoperability, not an override
of these rules. This corpus uses constructs common to 2.6 and 2.7.

### Representation and missing invariants

`smtlib::Lexer` returns parentheses and generic symbol tokens whose text still
contains quoting. `TokenSExprParser` turns other tokens into `SExpr::Atom`,
and `Parser::require_atom` returns raw text. `core::SymbolTable` and frontend
binding/definition maps compare those raw strings. Thus `gate` and `|gate|`
currently differ even though the standard says they identify one symbol.
Recognition of punctuation inside pipes is not sufficient for correctness.

The future fix must separate token category, original spelling and canonical
symbol identity. It must preserve the distinction between a numeral and a
quoted numeric-looking symbol, and between syntax keywords and quoted names.
Canonical identity must apply to term/sort lookup, binders, definitions,
duplicate checks and scope replay. Printing must encode that identity into a
legal spelling. Merely stripping pipes from every atom would conflate categories.
This is a specification-derived checklist, **not an implemented fix**.

The current lexer also accepts characters outside the simple-symbol alphabet,
backslashes/control characters in quoted symbols, and extra whitespace classes.
Its comment loop ends on LF only, although the standard also permits CR.
The command framer and lexer consequently disagree on a CR-only comment inside
a command. These paths require positive and negative tests independently of
symbol canonicalization.

## Online coverage inspected

No external solver test, grammar or implementation was copied into Primo.
All 55 cases in `benchmarks/lexical-conformance/cases.json` are original inputs
derived from the standard. The following public collections identify useful
coverage categories:

- cvc5 at `f9e5d3c312d9476763dc0afa85ac637da5907d99`:
  [quoted definitions](https://github.com/cvc5/cvc5/blob/f9e5d3c312d9476763dc0afa85ac637da5907d99/test/regress/cli/regress0/parser/quoted-define-fun.smt2),
  [non-ASCII quoted metadata](https://github.com/cvc5/cvc5/blob/f9e5d3c312d9476763dc0afa85ac637da5907d99/test/regress/cli/regress0/parser/non-std-char.smt2),
  and [quoted datatype constructors](https://github.com/cvc5/cvc5/blob/f9e5d3c312d9476763dc0afa85ac637da5907d99/test/regress/cli/regress0/parser/issue11763-quoted-dt-cons.smt2).
  Datatypes are outside Primo's supported surface; that test was inspected,
  not ported or presented as supported coverage.
- Dolmen at `5c9161a691872cee9ed0c58fe820a8b0b9e485ab`:
  [quoted reserved words](https://github.com/Gbury/dolmen/blob/5c9161a691872cee9ed0c58fe820a8b0b9e485ab/tests/parsing/smtlib/v2.7/pass/quoted_symbol.smt2)
  and [invalid characters inside quoted symbols](https://github.com/Gbury/dolmen/blob/5c9161a691872cee9ed0c58fe820a8b0b9e485ab/tests/parsing/smtlib/v2.7/errors/xxx_lexing_invalid_char_in_quoted_symbol.smt2),
  with expected diagnostic files. Its parsing tree also has many malformed
  identifier, attribute and binder cases. These are parser/type-checker tests,
  not an answer oracle.
- David Cok's [jSMTLIB tutorial](https://smtlib.github.io/jSMTLIB/SMTLIBTutorial.pdf),
  Section 6.1.5, describes a solver validation suite using scripts and expected
  responses, with error occurrence rather than exact diagnostic text compared.
  The current repository also publishes an
  [SMTTests suite](https://github.com/smtlib/jSMTLIB/tree/90ce411e7521b9f1f1338a3e2081ca3061547969/SMTTests),
  including token acceptance/rejection, declaration, definition, `let`, scope
  and version-specific groups. Its README distinguishes Java type-checker
  tests from tests forwarded to real solvers. We inspected the documentation
  and inventory, not executed its full JUnit suite; neither the historical
  tutorial nor its current golden files replace standard-derived expectations.

## Inspection of Griggio's generic parser

The user-provided [FBK parser](https://es-static.fbk.eu/people/griggio/misc/smtlib2parser.html)
ships as `smtlib2parser-1.4.tar.gz` (MIT; copyright 2010–2013). We inspected its
Flex lexer, Bison command entry, stream wrapper, parser interface and outer
command loop. The Yices sample solving backend was not inspected or executed.

Useful structural observations from the downloaded source:

- Quoted-symbol lexing returns a symbol token containing the interior name;
  the pipe delimiters are not part of backend symbol identity.
- The scanner uses separate token categories for symbols, numerals, strings
  and keywords instead of treating every non-parenthesis token as a name.
- The Bison entry accepts one command explicitly. The outer loop retains the
  scanner and calls it again; backend operations are callbacks.
- The file stream reads from `FILE*`. Scanner refills stop at a closing
  parenthesis instead of insisting on filling a large block. This is input
  chunking, not a second parser of complete command text.

Those mechanisms are inspection findings, not a verified live-pipe test or a
proposal to copy the implementation. They illustrate that parser generators
can support a command-at-a-time interface.

It is **not a current lexical conformance oracle**: its string state recognizes
backslash-escaped double quotes rather than the current doubled-quote syntax;
its quoted-symbol state admits backslash and control characters; its numeral
rule permits leading zeros; comments are LF-terminated. The archive contains
six sample backend inputs, not a demonstrated exhaustive conformance suite.
The download hash and inspection scope are recorded in the run log.

## Reproducible multi-solver test

```bash
python3 scripts/check_smtlib_lexical_conformance.py \
  --out results/lexical-conformance-new
```

The driver runs Primo, Z3, cvc5 in ordinary and strict modes, Yices, OpenSMT,
and Dolmen as a strict parser/type checker. It requires all selected binaries,
never installs tools implicitly, and supports `--only` for focused reruns.
Each input is executed as a file with a five-second timeout. The output folder
must be new. Raw byte-preserving inputs/stdout/stderr, executable versions and
hashes, exact commands, all query answers and a matrix are retained. This is
not an interactive liveness, model validation, fuzz or speed-measurement gate.

The classifier checks errors even after an answer, distinguishes crashes and
unexplained nonzero exits from syntax rejection, and requires every expected
query answer. Dolmen acceptance counts only as parsing/type acceptance. These
small fixtures avoid arbitrary user diagnostics; the classifier is not a
general SMT-LIB response parser. A nonzero audit exit is expected while the
documented conformance findings remain; no failures are hidden with xfail.
Normal unit tests exercise the classifier without requiring external solvers.

The final local run has 34 valid cases, 19 invalid cases and two explicitly
diagnostic cases (solver-reserved `.17` and duplicate definition parameters;
the latter is not assigned the separate `let` distinctness rule without a
stronger specification justification). There are 385 executions, no crashes
or timeouts, and unchanged executable hashes.

| Profile | Valid answer passes / 34 | Valid rejected | Wrong answers | Invalid rejected / 19 | Invalid accepted |
| --- | ---: | ---: | ---: | ---: | ---: |
| Primo | 21 | 11 | 2 | 3 | 15 |
| Z3 5.0.0 | 32 | 2 | 0 | 13 | 6 |
| cvc5 1.3.4 | 32 | 2 | 0 | 16 | 3 |
| cvc5 1.3.4 strict | 33 | 1 | 0 | 16 | 3 |
| Yices 2.7.0 | 32 | 2 | 0 | 16 | 3 |
| OpenSMT 2.9.2 | 31 | 2 | 0 | 11 | 8 |

Primo's remaining invalid case reports unsupported rather than a syntax error.
OpenSMT's remaining valid case produces a malformed response prefix before
UNSAT, so it is a response mismatch, not a wrong Boolean answer. Dolmen 0.10
accepts all 34 valid cases and rejects 17/19 invalid cases; it does not solve
the formulas. The committed matrix retains all exceptions and diagnostic rows.

Both quoted/unquoted shadowing cases return SAT in Primo versus UNSAT in every
reference solver. All references resolve the valid alias lookup cases. However,
even cvc5 strict accepts backslashes inside quoted symbols, whereas Yices and
Dolmen reject them. All tested solvers reject the valid CR-only comment case;
Dolmen accepts it. Every profile accepts the invalid duplicate-`let` case.
Thus a unanimous vote does not establish language conformance.

Primo fixes, model/print round trips, interactive fragmented-input tests, ASan
and fresh behavior-focused fuzzing remain required before merge or renewed
performance qualification. The artifacts and committed matrix record the
problem; they do not fix it.
