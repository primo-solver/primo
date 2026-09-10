# SMT-LIB lexical conformance audit

## Status and authoritative specification

### Implemented repair and selected compatibility policy

The selected policy follows the tested Z3 character extensions (comma in simple
names, backslash/control characters within quoted names), but rejects unquoted
reserved words and leading-zero numerals, including decimal integer parts.
Standard whitespace is TAB/LF/CR/space, and comments terminate on LF or CR.
Parallel duplicate `let` names use the tested Z3 last-binding-wins extension;
all right-hand sides still evaluate outside the new scope. Duplicate definition
parameters remain rejected; this is a documented limitation, not an inferred
standard requirement.

Specification-derived operations and invariants implemented:

- `Lexer` classifies symbols, reserved syntax, numerals, decimals, binary/hex
  literals, strings and keywords in linear time. Quoted symbol payloads lose
  only their delimiters. Strings retain their spelling. Unterminated tokens fail.
- `SExpr` owns token category and text; iterative deep copy preserves both.
  Categories distinguish reserved syntax from quoted names and literals from
  numeric-looking identifiers. No AST borrow survives mutation of its owner.
- `Parser` validates command argument categories; `TermBuilder` dispatches
  syntax and literals by category, and uses canonical names for all lookups.
  Existing hash maps retain expected linear total name-processing cost; no
  extra term arena or theory combination mechanism is introduced.
- `core::SymbolTable` receives uninterpreted canonical name strings, including
  the empty name. It does not parse quoting. IDs and duplicate checks are unchanged.
  `Symbol::internal` records auxiliary ownership explicitly; only
  `declare_internal_constant` sets it. Model completeness, printing and interface
  classification no longer mistake user `$primo_...` names for auxiliaries.
  Boolean-argument preprocessing recognizes that internal flag before its subtype
  prefix, preserving the prior treatment of genuine generated proxies.
- Model serialization quotes canonical names when necessary under this policy;
  model parameter freshness compares canonical identities, not printed spellings.
- Positive/negative category tests, both wrong-answer regressions, scope replay,
  typed AST copy, model round trips, fragmented live input, ASan and generated
  alias-mutation comparisons qualify the change. Compatibility acceptance is
  reported separately from normative conformance.

This is an original implementation based on SMT-LIB Sections 3.1–3.3,
3.6 and 4.2; the listed extensions are explicit user-selected deviations.

The historical audit of `9f20f58` exposed **two wrong-answer cases**, eleven rejected valid
cases, and multiple accepted invalid inputs. The repair above addresses these
under the selected policy; qualification is recorded separately below and in
the run log. Prior scoped fuzzing did not
exercise the quoted/unquoted name equivalence behind the wrong answers.

The source of truth is Barrett, Fontaine and Tinelli,
[SMT-LIB 2.7, release 2026-03-27](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-03-27.pdf):
Sections 3.1–3.3 specify lexical categories and identifiers; 3.6.1–3.6.2 specify
binding; 4.1.4–4.1.5 specify scoped declarations; Appendix B collects syntax.
Reference-solver acceptance is evidence about interoperability, not an override
of these rules. This corpus uses constructs common to 2.6 and 2.7.

### Representation and missing invariants

Previously, `smtlib::Lexer` returned generic symbol tokens retaining pipe
delimiters. Frontend and core maps compared those raw strings, making `gate`
and `|gate|` different. Recognizing punctuation inside pipes was insufficient.

The repair separates token category from canonical symbol identity. It preserves
the distinction between a numeral and a
quoted numeric-looking symbol, and between syntax keywords and quoted names.
Canonical identity applies to term/sort lookup, binders, definitions,
duplicate checks and scope replay. Printing encodes that identity into a spelling
legal under the selected policy. Merely stripping pipes from every atom would
conflate categories.

The repaired lexer deliberately accepts the selected character extensions, not
arbitrary simple-name characters or extra whitespace classes. Both lexer and
command framer recognize CR and LF comment endings. Numeric-looking quoted names
never reach the rational parser. Reserved-word recognition uses binary search
over a fixed sorted vocabulary; the framer shares the explicit whitespace rule.

### Representation and lifetime

`Token` owns its text; `Lexer` borrows input only for its lifetime. `Parser` owns
that input while `TokenSExprParser` is running. `SExpr::atom_kind` retains lexical
category and `SExpr::atom` owns canonical symbol text or literal spelling; the
iterative copy/destruction algorithms retain deep-input stack safety. Commands
and session journal entries own their ASTs. Symbol, definition and binder maps
receive canonical strings; numeric IDs retain their existing lifetime rules.
The core does not depend on SMT-LIB token classes. Model signatures copy the
internal-ownership flag with the rest of each `Symbol`.

This is not a full SMT-LIB implementation. ANTLR discovery/linkage, the unused
generated parser and grammar, and its installer have been removed: the active
handwritten parser is now the only parser in the build. CLI11, CaDiCaL/IPASIR
and GMP remain required; Java is not needed for parsing or building Primo.
Unsupported theories/commands remain unsupported. UTF-8 well-formedness is not separately
validated. Extension-bearing names may produce models rejected by a strictly
conforming external parser; standard-name models retain standard spellings.

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

# Enforce the selected Primo policy and compare generated standard inputs:
python3 scripts/check_smtlib_lexical_conformance.py --policy \
  --mutations 1000 --seed 0 --only primo z3 cvc5 \
  --out results/lexical-policy-new
```

The driver runs Primo, Z3, cvc5 in ordinary and strict modes, Yices, OpenSMT,
and Dolmen as a strict parser/type checker. It requires all selected binaries,
never installs tools implicitly, and supports `--only` for focused reruns.
Each input is executed as a file with a five-second timeout; Primo also receives
`--max-theory-conflicts=1000`. The output folder
must be new. Raw byte-preserving inputs/stdout/stderr, executable versions and
hashes, exact commands, all query answers and a matrix are retained. This is
not an interactive liveness, model validation or speed-measurement gate. The
optional mutation mode is a focused parser/binding fuzz gate, not a broad theory
campaign. It generates standard inputs across five logics, varying quoted names,
internal-looking user names, macro capture, parallel binding and scope replay.

`--policy` preserves each normative outcome but additionally recognizes the four
selected extensions for Primo. Fixed reference nonconformance remains visible
without failing the Primo policy gate. Every generated case must pass all selected
profiles; no reference majority voting is used. Original fixtures and the old
matrix/summary remain unchanged as historical evidence.

The classifier checks errors even after an answer, distinguishes crashes and
unexplained nonzero exits from syntax rejection, and requires every expected
query answer. Dolmen acceptance counts only as parsing/type acceptance. These
small fixtures avoid arbitrary user diagnostics; the classifier is not a
general SMT-LIB response parser. A nonzero audit exit is expected while the
documented conformance findings remain; no failures are hidden with xfail.
Normal unit tests exercise the classifier without requiring external solvers.

The historical local run has 34 valid cases, 19 invalid cases and two explicitly
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

The historical Primo binary returned SAT in both quoted/unquoted shadowing cases
versus UNSAT in every reference solver. All references resolved the valid alias lookup cases. However,
even cvc5 strict accepts backslashes inside quoted symbols, whereas Yices and
Dolmen reject them. All tested solvers reject the valid CR-only comment case;
Dolmen accepts it. Every profile accepts the invalid duplicate-`let` case.
Thus a unanimous vote does not establish language conformance.

### Repair qualification

The first repair probe (`results/lexical-fix-initial`) passes all 34 valid cases,
rejects 15 forbidden cases and accepts the four selected extensions. This probe
precedes the explicit internal-symbol metadata repair and is not its qualification.
The committed C++ tests cover both wrong answers, AST category copies, rejected
leading zeroes (including decimal integer parts), reserved-word declarations,
literal/name distinctions, empty symbols, parallel duplicate binding and replay.
Model tests reparse printed definitions and refute violations, including names
that resemble internal proxies; the live-pipe test fragments doubled quotes and
uses CR-only comments. Fresh clean-commit fuzz and external model evidence must
be recorded before treating the implementation milestone as fully qualified.
