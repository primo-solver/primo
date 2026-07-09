# Primo Scaffold Specification

You are creating the initial scaffold for **Primo**, a clean-room, paper-grounded SMT solver developed with LLM assistance.

Primo is a new project. It is **not** a continuation of LLM2SMT. The older solver may be used only as a reference artifact for comparison and retrospection, not as a source of code or text.

Predecessor reference:

```text
LLM2SMT, branch lra
https://github.com/MikolasJanota/llm2smt/tree/lra
```

## Project identity

**Name:** Primo

**Tagline:**

```text
Primo: an SMT solver from first principles, papers, and tests.
```

**Longer description:**

```text
Primo is a clean-room SMT solver whose implementation is systematically linked to the algorithms and papers it is based on.
```

The goal is to build a research-oriented SMT solver in a systematic way, with clear architecture, documented algorithmic sources, strong testing, fuzzing, benchmarking, and debugging infrastructure from the beginning.

The implementation should be original. Do not copy code from LLM2SMT, CVC4/CVC5, Z3, Yices, MathSAT, OpenSMT, or any other solver.

Important SAT-solver decision:

```text
Primo must not implement its own SAT solver.
Use IPASIR-UP as the SAT backend interface.
Use CaDiCaL as the initial IPASIR-UP implementation.
The `sat/` layer is an adapter and ownership boundary for SAT literals,
clauses, assumptions, backend calls, and solver results; it is not a
home-grown DPLL/CDCL implementation.
```

The project should use papers and original design notes as algorithmic sources. Every nontrivial algorithmic component must document the paper or design note it is based on.

Implementation language and build system:

```text
Use C++20.
Use CMake as the primary build system.
Provide a top-level Makefile as a convenience wrapper around CMake, tests,
scripts, and documentation commands.
Do not leave the implementation language or build system open-ended.
```

---

# 1. Initial implementation scope

The first milestone is a small but systematic SMT solver for **QF_UF**.

Initial supported features:

```text
SMT-LIB subset
typed terms
hash-consed term manager
symbol table
Boolean terms
equality
uninterpreted sorts
uninterpreted constants
uninterpreted functions
uninterpreted predicates
assert
check-sat
get-model later
push/pop later
Boolean abstraction
IPASIR-UP SAT backend interface
lazy DPLL(T)
QF_UF theory solver
congruence closure
disequality conflict detection
eventual explanation-producing conflict lemmas
differential testing against Z3/cvc5
external SMT-LIB fuzzing support
benchmark runner
delta-debugging/minimization scaffold
```

Explicit non-goals for the initial scaffold:

```text
full SMT-LIB support
quantifiers
arrays
datatypes
bit-vectors
strings
non-linear arithmetic
proof production
custom SAT solver implementation, including custom DPLL/CDCL
optimized Simplex
interpolation
parallel solving
```

Later targets:

```text
difference logic
LRA via Simplex
incremental solving
theory propagation
LIA extensions
model validation
proof logging
```

---

# 2. Papers to document from the beginning

Create documentation stubs for these papers. Do not claim that the algorithms are implemented until they are.

## 2.1 IPASIR-UP SAT backend interface

Katalin Fazekas, Aina Niemetz, Mathias Preiner, Markus Kirchweger, Stefan Szeider, Armin Biere:
**IPASIR-UP: User Propagators for CDCL.** SAT 2023.

Use as the basis for the SAT backend interface:

```text
standardized interaction with a CDCL SAT solver
incremental SAT solving API
user propagator callbacks
assignment/trail inspection
adding clauses during search
theory-guided propagation and conflict integration
CaDiCaL as the initial backend implementation
```

Initial implementation may use a minimal subset sufficient for lazy DPLL(T):

```text
create backend instance
add clauses
add assumptions
solve
read assignment/model values
detect failed assumptions where available
report unsupported callback features clearly
```

Do not implement a SAT solver. Do not implement custom CDCL. Do not copy CaDiCaL code. Treat CaDiCaL as an external dependency behind the IPASIR-UP adapter.

## 2.2 CVC4 architecture

Clark W. Barrett, Christopher L. Conway, Morgan Deters, Liana Hadarean, Dejan Jovanovic, Tim King, Andrew Reynolds, Cesare Tinelli:
**CVC4.** CAV 2011.

Use as architectural inspiration only:

```text
modular SMT engine
typed expressions
theory solvers
SMT-LIB orientation
model construction
incrementality
clear separation between SAT and theory reasoning
```

## 2.3 Congruence closure

Robert Nieuwenhuis, Albert Oliveras:
**Fast Congruence Closure and Extensions.**

Use as the basis for the QF_UF theory solver:

```text
ground EUF
congruence closure
equality reasoning
disequality conflict detection
eventual explanations
```

## 2.4 Linear arithmetic

Bruno Dutertre, Leonardo de Moura:
**A Fast Linear-Arithmetic Solver for DPLL(T).**

Use later as the basis for LRA:

```text
Simplex-style arithmetic solver
DPLL(T) integration
bounds
tableau
backtracking
theory propagation
strict inequalities
```

Do not implement the full arithmetic solver in the first scaffold. Create documentation and data-structure stubs only if useful.

## 2.5 Difference constraints

Scott Cotton, Oded Maler:
**Fast and Flexible Difference Constraint Propagation for DPLL(T).**

Use later as the basis for difference logic:

```text
constraints of form x - y <= c
weighted graph representation
negative-cycle conflict detection
eventual propagation
```

Do not implement difference logic in the first scaffold unless the QF_UF scaffold is complete.

---

# 3. Repository layout

Create this repository structure.

```text
primo/
  README.md
  AGENTS.md
  DESIGN.md
  TODO.md
  LICENSE
  Makefile
  CMakeLists.txt

  src/
    main.*

    cli/
      options.*
      driver.*

    smtlib/
      lexer.*
      parser.*
      ast.*
      printer.*
      commands.*
      logic.*

    core/
      ids.*
      symbol_table.*
      sort.*
      term.*
      term_manager.*
      substitution.*
      rewrite.*
      model.*

    sat/
      lit.*
      clause.*
      cnf.*
      ipasir_up.*
      cadical_backend.*
      trail_view.*
      assumptions.*
      trail.*

    smt/
      context.*
      assertion_stack.*
      preprocessing.*
      clausifier.*
      abstraction.*
      dpllt.*
      explanation.*
      check_sat.*
      model_builder.*

    theory/
      theory_solver.*
      equality/
        union_find.*
        congruence_closure.*
        euf_solver.*
      arithmetic/
        linear_polynomial.*
        difference_logic.*
        simplex_scaffold.*

    util/
      logging.*
      statistics.*
      error.*
      result.*

  tests/
    unit/
    regression/
    smtlib/
      smoke/
      qf_uf/
      qf_idl/
      qf_lra/

  benchmarks/
    smoke/
    seeds/
      qf_uf/
      qf_idl/
      qf_lra/
    regressions/
    failures/

  scripts/
    run_smtlib.py
    compare_with_z3.py
    fuzz_external.py
    minimize_smt2.py
    bench.py
    validate_model.py

  docs/
    _config.yml
    _toc.yml
    index.md
    architecture.md
    dpllt.md
    sat-ipasir-up.md
    euf.md
    arithmetic.md
    difference_logic.md
    literature.md
    experiments.md
    debugging.md
    notebooks/
    reference/
      llm2smt-retrospective.md
    papers/
      cvc4-architecture.md
      ipasir-up.md
      fast-congruence-closure.md
      fast-linear-arithmetic-dpllt.md
      difference-constraint-propagation.md

  skills/
    code-review/
      SKILL.md
    testing/
      SKILL.md
    fuzzing/
      SKILL.md
    benchmarking/
      SKILL.md
    documentation/
      SKILL.md
    literature-survey/
      SKILL.md
    performance/
      SKILL.md
    delta-debugging/
      SKILL.md
```

Use `.hpp` and `.cpp` for C++ source files unless a narrower local convention is introduced. Prefer a minimal working CMake project with build/test commands.

Documentation workhorse:

```text
Use classic Jupyter Book, matching the `llm2smt` `lra` branch documentation setup.
Author normal documentation as MyST Markdown files.
Use notebooks only for executable experiments, benchmark analysis, and plotted reports.
Do not put ordinary architecture, design, or paper notes in notebooks.
The documentation must build locally with `make docs`.
`make docs` should call `scripts/build_docs.sh`, which creates `.venv-docs` when needed and builds static HTML under `docs/_build/html`.
```

---

# 4. Clean-room policy

Create this section in `AGENTS.md`.

```markdown
## Clean-room policy

Primo is a new solver, not a continuation of LLM2SMT.

The old repository may be used only for:

- understanding previous design decisions
- identifying missing tests
- comparing behavior
- extracting benchmark ideas
- writing retrospectives
- checking which development practices failed or succeeded

Do not copy source code, generated code, parser grammars, build files, comments, tests, or documentation text from LLM2SMT or from other SMT solvers.

When implementing an algorithm, use:

- the cited paper
- our own design notes
- newly written tests
- newly written code

If a design choice is inspired by LLM2SMT, document it as a retrospective note, not as an implementation source.
```

---

# 5. Paper-grounded implementation rule

Create this section in `AGENTS.md`.

```markdown
## Paper-grounded implementation rule

This solver is implemented from papers and original design notes.

Do not copy code from existing SMT solvers.

Every nontrivial algorithmic component must cite the paper, book chapter, or design note it is based on.

For each such component, include:

- bibliographic reference
- section or theorem used, where applicable
- implemented subset
- deviations from the paper
- known limitations
- tests derived from the algorithm

Code comments should identify algorithmic basis without overclaiming.
```

Example code header to use for future implementation files:

```text
Algorithmic basis:
  Nieuwenhuis and Oliveras, "Fast Congruence Closure and Extensions"

Implemented subset:
  Ground EUF congruence closure with disequality conflict detection.

Deviations:
  - initial version may rebuild state per theory check
  - no theory propagation initially
  - explanations may be added in a later milestone

Known limitations:
  - no arrays
  - no quantifiers
  - no proof production
```

---

# 6. Architecture rules

Create these rules in `AGENTS.md`.

```markdown
## Architectural rules

Preserve these boundaries:

- `smtlib/` parses, prints, and type-checks SMT-LIB.
- `core/` owns sorts, symbols, terms, rewriting, substitutions, and models.
- `sat/` owns propositional variables, literals, clauses, assumptions, and the IPASIR-UP backend adapter.
- `smt/` owns Boolean abstraction, clausification, assertion stacks, and DPLL(T).
- `theory/` owns theory solvers and theory-specific explanations.
- `scripts/` contains reproducible command-line workflows.
- `benchmarks/` contains only input instances and benchmark metadata.
- `docs/` contains the MyST/Jupyter Book documentation site, design notes, literature summaries, and optional experiment notebooks.

Do not introduce cross-layer shortcuts.

In particular:

- SMT-LIB AST nodes must not be used by the SAT or theory solvers.
- SAT literals must not directly contain terms.
- Theory solvers must communicate via theory literals and explanations.
- All terms must be created through the term manager.
```

---

# 7. Core invariants

Create these in `AGENTS.md` and `DESIGN.md`.

```markdown
## Core invariants

### Term invariant

- Terms are hash-consed.
- Term equality is by `TermId`.
- Every term has an explicit sort.
- Boolean formulas are terms of sort `Bool`.
- Applications are represented uniformly.
- All terms are created through the term manager.

### SAT invariant

- Primo does not implement its own SAT solver.
- SAT solving is delegated through IPASIR-UP, initially backed by CaDiCaL.
- The SAT backend sees only clauses over SAT literals.
- SMT atoms are mapped to SAT variables by the SMT layer.
- The SAT backend must not inspect theory terms.
- Assumptions are distinct from permanent clauses.
- Backend-specific details must stay behind the SAT adapter.

### Theory invariant

A theory solver receives asserted theory literals.

It may return:

- consistent
- conflict clause
- propagated literal with explanation
- model fragment

### DPLL(T) invariant

- The first implementation may use lazy theory checking.
- Theory propagation is optional until the EUF checker is correct.
- Every generated theory lemma must be independently checkable by tests.
```

---

# 8. Initial lazy DPLL(T) design

Document this in `docs/dpllt.md`.

The first DPLL(T) loop may be naive and lazy.

```text
loop:
  result = sat_backend.solve()

  if result == unsat:
      return unsat

  assignment = sat_backend.model()
  theory_literals = abstraction.true_theory_literals(assignment)

  for each theory solver:
      theory_solver.reset_or_rebuild()
      theory_solver.assert_relevant_literals(theory_literals)

  if all theory solvers are consistent:
      return sat

  conflict = some_theory_solver.conflict_clause()
  sat_backend.add_clause(conflict)
```

The first implementation may rebuild theory state from the current SAT assignment on each check. This is acceptable for the scaffold. Later milestones may add incremental theory state, propagation, and backtracking. Do not add a fallback in-house SAT solver; if CaDiCaL/IPASIR-UP is missing, report the missing dependency clearly.

---

# 9. Initial QF_UF target

Implement or scaffold the following interface.

```text
EufSolver:
  push()
  pop(n)
  assert_eq(t1, t2, source_lit)
  assert_neq(t1, t2, source_lit)
  check() -> sat | conflict
  conflict_clause() -> list[SatLit]
  are_equal(t1, t2) -> bool
  explain_eq(t1, t2) -> list[TheoryLit]
```

Initial QF_UF milestones:

```text
EUF0: union-find over TermId
EUF1: register application terms
EUF2: merge equality classes
EUF3: maintain congruence closure
EUF4: support disequality constraints
EUF5: detect equality/disequality conflicts
EUF6: produce conflict clauses
EUF7: produce explanations
EUF8: produce models
```

For the first scaffold, implement only what is feasible and create TODOs for the rest.

---

# 10. Required smoke benchmarks

Create small hand-written SMT-LIB files under:

```text
benchmarks/smoke/qf_uf/
```

At minimum, create these cases.

## 10.1 Direct equality contradiction

```lisp
(set-logic QF_UF)
(declare-sort A 0)
(declare-fun a () A)
(declare-fun b () A)
(assert (= a b))
(assert (not (= a b)))
(check-sat)
```

Expected:

```text
unsat
```

## 10.2 Unary congruence contradiction

```lisp
(set-logic QF_UF)
(declare-sort A 0)
(declare-fun f (A) A)
(declare-fun a () A)
(declare-fun b () A)
(assert (= a b))
(assert (not (= (f a) (f b))))
(check-sat)
```

Expected:

```text
unsat
```

## 10.3 Binary congruence contradiction

```lisp
(set-logic QF_UF)
(declare-sort A 0)
(declare-fun f (A A) A)
(declare-fun a () A)
(declare-fun b () A)
(declare-fun c () A)
(declare-fun d () A)
(assert (= a b))
(assert (= c d))
(assert (not (= (f a c) (f b d))))
(check-sat)
```

Expected:

```text
unsat
```

## 10.4 Satisfiable uninterpreted constants

```lisp
(set-logic QF_UF)
(declare-sort A 0)
(declare-fun a () A)
(declare-fun b () A)
(assert (not (= a b)))
(check-sat)
```

Expected:

```text
sat
```

## 10.5 Satisfiable nested functions

```lisp
(set-logic QF_UF)
(declare-sort A 0)
(declare-fun f (A) A)
(declare-fun g (A) A)
(declare-fun a () A)
(declare-fun b () A)
(assert (= (f (g a)) b))
(check-sat)
```

Expected:

```text
sat
```

---

# 11. Difference-logic future stub

Create documentation and possibly empty module stubs for difference logic, but do not prioritize implementation.

Document this representation in `docs/difference_logic.md`.

```text
x - y <= c
means
x <= y + c

Graph encoding:
edge y -> x with weight c
```

Unsatisfiability is detected by a negative cycle.

Example future test:

```lisp
(set-logic QF_IDL)
(declare-fun x () Int)
(declare-fun y () Int)
(assert (<= (- x y) 3))
(assert (<= (- y x) -4))
(check-sat)
```

Expected:

```text
unsat
```

Reason:

```text
x - y <= 3
y - x <= -4
sum gives 0 <= -1
```

---

# 12. LRA future stub

Create documentation and possibly empty module stubs for LRA, but do not implement full Simplex in the initial scaffold.

Future LRA data structures:

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

Initial LRA TODOs only:

```text
represent rational coefficients
represent linear polynomials
normalize arithmetic atoms
define bounds
define tableau rows
add parser/type-checker tests later
```

---

# 13. Scripts to create

Create these scripts with stable CLIs. If the implementation is not ready, create useful skeletons with clear TODOs and graceful error messages.

## 13.1 `scripts/run_smtlib.py`

Purpose:

```text
Run Primo on one or more SMT-LIB files.
```

Desired CLI:

```bash
python scripts/run_smtlib.py benchmarks/smoke/qf_uf/*.smt2 \
  --solver ./primo \
  --timeout 5
```

## 13.2 `scripts/compare_with_z3.py`

Purpose:

```text
Run Primo and Z3/cvc5 on the same files and compare sat/unsat/unknown.
```

Desired CLI:

```bash
python scripts/compare_with_z3.py benchmarks/smoke/qf_uf/*.smt2 \
  --solver ./primo \
  --refs z3 cvc5 \
  --timeout 5 \
  --out results/compare.csv
```

Classify outcomes:

```text
PASS
CRASH
TIMEOUT
OUR_SAT_REF_UNSAT
OUR_UNSAT_REF_SAT
REF_DISAGREEMENT
UNKNOWN_INVOLVED
UNSUPPORTED_INPUT
PARSER_REJECTS_REFERENCE_ACCEPTED
```

## 13.3 `scripts/fuzz_external.py`

Purpose:

```text
Wrap external SMT-LIB fuzzers behind one interface.
```

Support at least these names as options:

```text
yinyang
fuzzsmt
storm
```

Do not require them to be installed for normal tests. If missing, skip with a clear message.

Desired CLI:

```bash
python scripts/fuzz_external.py \
  --fuzzer yinyang \
  --seeds benchmarks/seeds/qf_uf \
  --solver ./primo \
  --refs z3 cvc5 \
  --logic QF_UF \
  --count 1000 \
  --timeout 5 \
  --out fuzz-results/qf_uf-yinyang
```

The wrapper should:

```text
take a seed directory
generate mutants or random instances
run Primo
run Z3 and cvc5 as references
classify outcomes
save failures
save unsupported inputs separately
emit a CSV summary
preserve the random seed and exact command line
```

## 13.4 `scripts/minimize_smt2.py`

Purpose:

```text
Minimize failing SMT-LIB inputs while preserving a failure predicate.
```

Desired CLI:

```bash
python scripts/minimize_smt2.py \
  --input benchmarks/failures/seed-42.smt2 \
  --predicate "python scripts/compare_with_z3.py {} --expect-disagreement" \
  --output benchmarks/regressions/reduced-seed-42.smt2
```

Initial reduction passes:

```text
remove assertions
remove unused declarations
simplify Boolean structure
replace subterms by constants of same sort
shrink term depth
preserve well-sortedness
```

Skeleton is acceptable.

## 13.5 `scripts/bench.py`

Purpose:

```text
Run reproducible benchmarks and emit CSV.
```

Desired CLI:

```bash
python scripts/bench.py benchmarks/smoke/qf_uf/*.smt2 \
  --solver ./primo \
  --timeout 5 \
  --out results/bench.csv
```

Collect:

```text
file
result
runtime
timeout
exit_code
stdout path
stderr path
optional solver statistics
```

## 13.6 `scripts/validate_model.py`

Purpose:

```text
Validate produced models where possible.
```

Skeleton is acceptable. It may initially print:

```text
model validation is not implemented yet
```

---

# 14. Failure taxonomy

Create `docs/debugging.md` with this taxonomy.

```markdown
# Failure taxonomy

## Result failures

- `UNSOUND_SAT`: Primo says sat, trusted solver says unsat.
- `UNSOUND_UNSAT`: Primo says unsat, trusted solver says sat.
- `UNKNOWN_REGRESSION`: Primo now says unknown on a previously solved case.
- `REF_DISAGREEMENT`: reference solvers disagree.

## Runtime failures

- `CRASH`
- `ASSERTION_FAILURE`
- `TIMEOUT`
- `MEMORY_LIMIT`
- `NONTERMINATION`

## Model failures

- `INVALID_MODEL`
- `PARTIAL_MODEL`
- `SORT_INCORRECT_MODEL`
- `FUNCTION_INTERPRETATION_MISSING_DEFAULT`

## Infrastructure failures

- `PARSER_REJECTS_VALID_INPUT`
- `PRINTER_PRODUCES_INVALID_SMTLIB`
- `TYPECHECKER_ACCEPTS_INVALID_INPUT`
- `NONDETERMINISTIC_OUTPUT`
```

---

# 15. Skills

Create the following skill directories and `SKILL.md` files.

## 15.1 `skills/code-review/SKILL.md`

```markdown
---
name: smt-code-review
description: Review SMT solver code for architectural violations, solver unsoundness, missing invariants, bad explanations, and insufficient tests.
---

# SMT solver code review skill

When reviewing code, prioritize correctness over style.

## Review order

1. Soundness
2. Invariant preservation
3. Layering violations
4. Incrementality and backtracking correctness
5. Explanation correctness
6. Model construction correctness
7. Test coverage
8. Performance risks
9. Code clarity

## Things to look for

### Term layer

Check:

- all terms are created through the term manager
- sorts are checked at construction
- structurally equal terms share the same `TermId`
- rewrite rules preserve sorts
- pretty-printing round-trips through the parser where expected

### SAT layer

Check:

- clauses contain only SAT literals
- SAT solving goes through the IPASIR-UP adapter
- CaDiCaL-specific calls are isolated in the backend module
- permanent clauses and assumptions are passed to the backend with correct lifetimes
- learned or theory clauses sent to the backend are not tautological unless intentionally allowed
- assumptions are handled separately from permanent clauses
- model values are queried only after SAT result is SAT

### SMT layer

Check:

- Boolean abstraction is total and deterministic
- each SMT atom has exactly one SAT variable
- polarity is handled correctly
- assertion stack push/pop restores all relevant state
- generated theory lemmas are valid

### Theory layer

Check:

- theory literals are asserted incrementally in trail order
- backtracking restores all theory state
- conflicts include enough explanation literals
- explanations are implied by the asserted literals
- disequality conflicts in EUF are explained by equality paths

### Model generation

Check:

- every declared constant has an interpretation
- every declared function has default values
- values respect equivalence classes
- model validation script accepts produced models where possible

## Output format

Use this format:

1. Summary
2. Blocking issues
3. Soundness risks
4. Missing tests
5. Suggested patch plan
6. Non-blocking cleanup

Do not rewrite large files unless explicitly asked.
```

## 15.2 `skills/testing/SKILL.md`

```markdown
---
name: smt-testing
description: Design and implement unit, regression, differential, and SMT-LIB smoke tests for the SMT solver.
---

# SMT solver testing skill

## Goal

Add tests that would catch solver unsoundness, incompleteness in the intended fragment, parser/type errors, and model-generation bugs.

## Test levels

### Unit tests

Use for:

- term manager
- sort checker
- parser
- rewriter
- union-find
- congruence closure
- SAT clauses
- Boolean abstraction

### Regression tests

Use for:

- previously failing SMT-LIB files
- reduced fuzzing failures
- solver crashes
- wrong sat/unsat answers
- invalid models

### Differential tests

Compare result with trusted solvers:

- Z3
- cvc5

Only compare `sat`, `unsat`, and `unknown` unless model validation is implemented.

### Smoke tests

Keep tiny hand-written SMT-LIB files in `benchmarks/smoke`.

## Required test classes for QF_UF

Include:

- reflexive equality
- equality transitivity
- congruence through unary functions
- congruence through binary functions
- equality/disequality contradiction
- uninterpreted predicate abstraction
- nested function applications
- multiple uninterpreted sorts
- push/pop around conflicts
- model generation for constants and functions

## Test-generation policy

When adding a feature:

1. Add positive tests.
2. Add negative/type-error tests if applicable.
3. Add at least one SMT-LIB regression test.
4. Add a differential test if Z3/cvc5 supports the fragment.

## Output format

When asked to add tests:

- explain the missing coverage
- add minimal tests
- run the relevant test command
- report failures precisely
```

## 15.3 `skills/fuzzing/SKILL.md`

```markdown
---
name: smt-fuzzing
description: Generate, run, reduce, and triage SMT-LIB fuzz tests for solver fragments such as QF_UF and QF_LIA.
---

# SMT solver fuzzing skill

## Goal

Find wrong answers, crashes, assertion failures, nontermination, invalid models, and parser bugs.

## Fuzzing modes

### Generator-based fuzzing

Generate random well-sorted SMT-LIB formulas for a fixed logic.

Start with:

- QF_UF
- QF_LIA only after arithmetic is implemented

### Differential fuzzing

For each generated instance:

1. Run Primo.
2. Run Z3.
3. Run cvc5 if available.
4. Compare results.

Classify:

- same result
- solver crash
- timeout
- Primo `sat` vs reference `unsat`
- Primo `unsat` vs reference `sat`
- reference disagreement
- unknown involved

### Metamorphic fuzzing

Apply transformations expected to preserve satisfiability:

- reorder assertions
- duplicate assertions
- add `(assert true)`
- rename symbols
- replace `t = t` with `true`
- split conjunctions
- wrap with push/pop

## QF_UF generation rules

Generate:

- uninterpreted sorts
- constants
- functions of arity 0, 1, 2, occasionally 3
- predicate symbols
- equalities
- disequalities
- Boolean combinations
- shallow and deep terms

Respect:

- sort correctness
- declared symbols only
- bounded term depth
- bounded assertion count

## External fuzzers

Support wrappers for:

- Yin-Yang / OpFuzz / TypeFuzz
- FuzzSMT
- STORM, later
- Murxla, later if an API exists

Do not require external fuzzers to be installed for normal tests.

## Failure handling

For every failure:

1. Save original input.
2. Save solver outputs.
3. Run reducer if available.
4. Save minimized instance in `benchmarks/regressions`.
5. Add expected behavior.
6. Create a short bug note.

## Output format

Report:

- seed
- logic
- number of generated instances
- number of crashes
- number of disagreements
- number of timeouts
- path to saved failures
- exact command to reproduce
```

## 15.4 `skills/benchmarking/SKILL.md`

```markdown
---
name: smt-benchmarking
description: Run reproducible solver benchmarks, compare solver versions, collect statistics, and produce benchmark reports.
---

# SMT solver benchmarking skill

## Goal

Measure solver performance reproducibly.

## Rules

Never report performance without:

- exact commit hash
- benchmark set
- timeout
- machine info if available
- command line
- number of solved instances
- runtime distribution
- timeout count
- error count

## Benchmark categories

Use separate benchmark sets:

- smoke: tiny correctness tests
- regression: previous bugs
- development: medium hand-picked instances
- external: SMT-LIB or other imported benchmark suites
- fuzzed: generated instances

## Metrics

Collect:

- result: sat/unsat/unknown/error/timeout
- wall-clock time
- memory if available
- number of SAT conflicts
- number of decisions
- number of theory checks
- number of theory conflicts
- number of lemmas
- number of propagations
- number of terms
- number of clauses

## Comparison mode

Compare:

- baseline commit
- current commit

Report:

- solved delta
- median runtime ratio
- geometric mean where meaningful
- regressions above threshold
- improvements above threshold

## Output format

Produce:

1. Benchmark configuration
2. Summary table
3. Regressions
4. Improvements
5. Suspicious results
6. Reproduction commands

Do not overinterpret small benchmark sets.
```

## 15.5 `skills/documentation/SKILL.md`

```markdown
---
name: smt-documentation
description: Write code-grounded architecture, algorithm, invariant, representation, paper-traceability, and user documentation for the SMT solver. Use when documentation should explain concrete implementation details such as term storage, ownership, memory management, hash-consing, SAT/theory interfaces, or how papers inform the code.
---

# SMT solver documentation skill

## Goal

Document the solver so that future code changes preserve the architecture, representations, paper-grounded design choices, and invariants.

Use classic Jupyter Book, matching the `llm2smt` `lra` branch documentation setup:

- author normal docs in MyST Markdown
- maintain `docs/_config.yml`, `docs/_toc.yml`, and `docs/index.md`
- use notebooks only for executable experiments and benchmark reports
- keep the documentation buildable with `make docs`
- keep `scripts/build_docs.sh` as the canonical build wrapper

## Documentation types

### Architecture docs

Explain:

- module boundaries
- ownership and lifetime of data structures
- memory management strategy
- value identity versus object identity
- mutability and aliasing rules
- hash-consing or interning strategy
- stable and unstable identifiers
- major invariants
- extension points

Architecture docs must be code-grounded. Before writing or revising an architecture page:

1. read the relevant headers and implementations
2. cite the concrete local types, methods, and files being described
3. describe what is implemented now, not the intended future design
4. include known limitations and unsupported paths

### Algorithm docs

Explain:

- Boolean abstraction
- clausification
- DPLL(T)
- IPASIR-UP/CaDiCaL SAT backend integration
- congruence closure
- model generation
- push/pop
- explanation generation

Algorithm docs must connect mechanisms to representations. For each algorithmic component, include:

- concrete state owned by the component
- input and output types
- invariants before and after each public operation
- how errors, unsupported input, and unknown results are represented
- complexity expectations when they are known or inferable
- where the implementation intentionally diverges from the paper

### User docs

Explain:

- supported SMT-LIB fragment
- build instructions
- run instructions
- options
- examples
- known limitations

### Developer docs

Explain:

- how to add a theory
- how to add an SMT-LIB construct
- how to add a regression test
- how to run fuzzing
- how to run benchmarks
- how to build the Jupyter Book documentation

## Documentation gate

Do not treat an implementation task as complete until the documentation says how the implemented representation works.

For any change to parser, terms, symbols, SAT integration, clausification, theory solving, options, or solver control flow:

1. identify the nearest documentation page before editing code
2. update that page with the implemented C++ types, ownership, invariants, and limitations
3. explain how paper-backed ideas are used, partially used, or deferred
4. run `make docs`
5. commit documentation with the implementation or in an adjacent commit

If the implementation intentionally omits documentation, state that explicitly in the final answer as a remaining blocker.

## Required Representation Sections

When documenting a core subsystem, include a "Representation" section. Answer these questions explicitly when relevant:

- What are the primary C++ types and where are they defined?
- Who owns the memory?
- Are objects moved, copied, interned, or referenced by numeric ids?
- Which ids or references remain stable after insertion?
- What is hash-consed, canonicalized, or deduplicated?
- What equality notion is used: syntactic identity, semantic equality, SAT literal equality, or congruence equality?
- Which operations may allocate new state?
- Which operations are pure queries?
- What invalid states are prevented by constructors or checked dynamically?

For term documentation specifically, cover:

- term node storage
- `TermId` stability
- hash-consing keys
- sort checking
- Boolean simplification
- interaction with symbol declarations
- how applications, equalities, and Boolean connectives are represented

## Paper Traceability

Every page that mentions a paper-backed component must include a "Paper Traceability" or "Sources and Implementation Status" section:

- list the relevant papers or notes
- state which ideas are implemented now
- state which ideas are only planned or deliberately deferred
- map each implemented idea to the local module or type where it appears
- avoid implying that a paper was fully implemented when only a small interface or stub exists

## Style

Use precise technical prose.
Prefer small examples.

Every nontrivial algorithm doc should include:

- input
- output
- representation
- invariants
- ownership and lifetime
- failure modes
- one worked example
- sources and implementation status

## Output format

When documenting a feature:

1. identify what changed
2. update the nearest relevant doc
3. read the relevant source files and document the actual representation
4. add or update paper traceability if papers motivate the design
5. add examples if useful
6. check that docs do not claim unsupported features
7. run `make docs` when documentation tooling is available
```

## 15.6 `skills/literature-survey/SKILL.md`

````markdown
---
name: smt-literature-survey
description: Survey SMT, SAT, DPLL(T), congruence closure, arithmetic, model construction, and solver-engineering papers for implementation-relevant ideas.
---

# SMT literature survey skill

## Goal

Extract implementation-relevant information from papers.

## Survey output format

For each paper, report:

1. Citation
2. Problem addressed
3. Core algorithmic idea
4. Required data structures
5. Important invariants
6. Complexity claims
7. Engineering tricks
8. What we should implement
9. What we should not implement yet
10. Relation to Primo's architecture

## Solver implementation focus

Prefer details about:

- DPLL(T)
- SAT backend APIs
- IPASIR-UP
- CaDiCaL integration
- congruence closure
- explanation-producing union-find
- Nelson-Oppen
- Simplex for LRA/LIA
- branch-and-bound
- cutting planes
- model construction
- SMT-LIB compliance
- proof logging
- incremental solving

## Literature notes location

Write summaries to:

`docs/literature.md`

Use this format:

```markdown
## Paper title

Bibliographic data.

### Relevance

### Algorithm

### Data structures

### Implementation notes

### Risks

### Possible milestone
````

## Rules

Do not claim to have read a paper unless the paper text or notes are available.
When uncertain, mark the claim as uncertain.
Prefer primary sources over blog posts.

Separate:

* what the paper says
* what we infer for Primo

````

## 15.7 `skills/performance/SKILL.md`

```markdown
---
name: smt-performance-improvement
description: Profile SMT solver performance, identify bottlenecks, propose optimizations, and validate them with benchmarks.
---

# SMT performance improvement skill

## Goal

Improve performance without breaking soundness.

## Rules

Do not optimize without a baseline measurement.
Do not change algorithmic behavior without tests.
Do not report speedups without benchmark evidence.

## Workflow

1. Select benchmark set.
2. Record baseline.
3. Profile.
4. Identify bottleneck.
5. Propose small change.
6. Implement change.
7. Run unit and regression tests.
8. Re-run benchmark.
9. Report improvement/regression.

## Common SMT solver bottlenecks

Check:

- term allocation
- hash-consing
- substitution/rewrite loops
- Boolean abstraction duplication
- clause creation
- SAT backend call overhead
- excessive assumptions or redundant clauses sent through IPASIR-UP
- repeated theory checks
- congruence closure rebuilds
- push/pop copying
- model construction over all terms instead of relevant terms only
- excessive pretty-printing/logging

## Acceptable optimizations

Prefer:

- interning
- memoization with scoped invalidation
- trail-based undo
- avoiding repeated AST traversal
- compact literal representation
- deterministic caches
- lazy model construction
- incremental congruence closure

Avoid initially:

- unsafe mutation
- global caches without clear invalidation
- parallelism
- complex heuristics without benchmark support

## Output format

Report:

1. baseline
2. profile finding
3. patch summary
4. correctness checks
5. benchmark comparison
6. remaining bottlenecks
````

## 15.8 `skills/delta-debugging/SKILL.md`

```markdown
---
name: smt-delta-debugging
description: Reduce failing SMT-LIB inputs, isolate wrong-answer bugs, and turn minimized failures into regression tests.
---

# SMT delta debugging skill

## Goal

Given a failing SMT-LIB input, minimize it while preserving the failure.

## Failure kinds

Support:

- crash
- timeout
- parser error
- wrong sat/unsat answer
- invalid model
- assertion failure
- reference solver disagreement

## Workflow

1. Confirm the failure.
2. Record exact reproduction command.
3. Classify the failure.
4. Run syntactic reduction.
5. Run semantic-preserving reductions.
6. Re-check against reference solvers.
7. Save minimized file under `benchmarks/regressions`.
8. Add a regression test.
9. Explain the likely bug location.

## SMT-LIB reductions

Try:

- remove assertions
- simplify Boolean structure
- replace subterms by constants of same sort
- remove unused declarations
- reduce function arities where possible
- reduce number of sorts
- inline `let`
- eliminate redundant equalities
- shrink term depth
- shrink numerals for arithmetic
- replace complex terms by fresh constants

## Validity constraints

Preserve:

- well-sortedness
- declared-before-used symbols
- selected logic if possible
- original failure kind

## Output format

Report:

- original file
- minimized file
- original size
- minimized size
- failure predicate
- reproduction command
- suspected component
```

---

# 16. README content

Create `README.md` with this content.

````markdown
# Primo

Primo is a clean-room, paper-grounded SMT solver developed with LLM assistance.

Its SMT layers are built from first principles, papers, and tests. The project starts from scratch and does not reuse code from existing SMT solvers or from its predecessor, LLM2SMT. SAT solving is delegated through IPASIR-UP, initially using CaDiCaL as the backend.

## Goals

- implement a small but systematic SMT solver
- keep the architecture modular and inspectable
- document every nontrivial algorithmic component with its paper basis
- prioritize correctness infrastructure before performance
- support reproducible testing, fuzzing, benchmarking, and debugging

## Initial target

The first target is QF_UF:

- SMT-LIB subset
- typed terms
- Boolean abstraction
- IPASIR-UP/CaDiCaL SAT backend integration
- lazy DPLL(T)
- congruence closure
- equality and disequality reasoning
- eventual model generation

## Later targets

- difference logic
- LRA via Simplex
- incrementality
- theory propagation
- LIA extensions

## Clean-room policy

Primo is a new solver. LLM2SMT may be used as a reference artifact only, not as a source of code or text.

## Build

TODO.

## Run

TODO.

## Test

TODO.

## Fuzz

TODO.

## Benchmark

TODO.

## Docs

Documentation is written in MyST Markdown and built with Jupyter Book.

```bash
make docs
```

Use notebooks only for executable experiments and benchmark reports.

## Literature

See `docs/papers/` and `docs/literature.md`.
````

---

# 17. Documentation system

Use classic Jupyter Book, matching the `llm2smt` `lra` branch documentation setup.

If Python tooling is used to manage documentation dependencies, include Jupyter Book in `docs/requirements.txt`. Do not require a global install.

Create `docs/_config.yml`.

```yaml
title: Primo
author: Primo contributors
only_build_toc_files: true
execute:
  execute_notebooks: "off"
```

Create `docs/_toc.yml`.

```yaml
format: jb-book
root: index
chapters:
  - file: architecture
  - file: sat-ipasir-up
  - file: dpllt
  - file: euf
  - file: arithmetic
  - file: difference_logic
  - file: debugging
  - file: literature
  - file: papers/ipasir-up
  - file: papers/cvc4-architecture
  - file: papers/fast-congruence-closure
  - file: papers/fast-linear-arithmetic-dpllt
  - file: papers/difference-constraint-propagation
  - file: reference/llm2smt-retrospective
```

Create `docs/index.md`.

```markdown
# Primo documentation

Primo is a clean-room, paper-grounded SMT solver developed with LLM assistance.

This documentation is authored in MyST Markdown and built with Jupyter Book.

## Documentation policy

- Use MyST Markdown for architecture, design, algorithm, paper, and developer notes.
- Use notebooks only for executable experiments, benchmark analysis, and plotted reports.
- Keep claims synchronized with implemented functionality.
- Cite the paper or design note behind each nontrivial algorithmic component.
- Keep dependency integration notes separate from algorithm descriptions.

## Main sections

- Architecture and invariants
- IPASIR-UP/CaDiCaL SAT backend integration
- Lazy DPLL(T)
- EUF/congruence closure
- Future arithmetic theories
- Debugging, experiments, and literature
```

Create `docs/sat-ipasir-up.md`.

```markdown
# SAT backend: IPASIR-UP and CaDiCaL

## Role

Primo does not implement a SAT solver.

The SAT layer owns Primo's propositional variables, literals, clauses, assumptions, and backend adapter. Actual SAT solving is delegated through IPASIR-UP, initially using CaDiCaL as the implementation.

## Initial scope

- create and destroy a backend instance
- add permanent clauses
- add assumptions for a solve call
- call solve
- read Boolean assignments after SAT
- identify failed assumptions where supported
- return clear unsupported-feature errors for callbacks not wired yet

## Deferred scope

- online theory propagation through user-propagator callbacks
- theory-guided decisions
- adding clauses during search
- explaining user propagations
- proof-producing integration

## Boundaries

- The backend sees only SAT literals and clauses.
- SMT terms and theory literals do not cross into CaDiCaL-specific code.
- CaDiCaL-specific APIs stay isolated in the backend module.
- Missing IPASIR-UP/CaDiCaL dependencies must be reported explicitly.

## References

See `docs/papers/ipasir-up.md`.
```

---

# 18. LLM2SMT retrospective note

Create `docs/reference/llm2smt-retrospective.md`.

````markdown
# LLM2SMT retrospective

## Status

LLM2SMT, branch `lra`, is the predecessor of Primo.

Repository:

```text
https://github.com/MikolasJanota/llm2smt/tree/lra
```

## How it may be used

Allowed:

* compare high-level architecture
* inspect old benchmark workflow
* identify missing regression tests
* understand previous failure modes
* compare command-line behavior
* document lessons learned

Not allowed:

* copying code
* copying tests verbatim
* copying grammar files
* copying documentation text
* porting modules mechanically

## Questions to answer later

* Which components were useful?
* Which parts became hard to maintain?
* Which invariants were missing?
* Which tests were absent?
* Which prompts led to unsystematic development?
* Which scripts should be redesigned?
* Which algorithms need stronger paper-level documentation?

````

---

# 19. Paper note templates

Create these five files.

## 19.1 `docs/papers/ipasir-up.md`

```markdown
# IPASIR-UP: User Propagators for CDCL

## Citation

Katalin Fazekas, Aina Niemetz, Mathias Preiner, Markus Kirchweger, Stefan Szeider, Armin Biere:  
IPASIR-UP: User Propagators for CDCL. SAT 2023.

## Role in Primo

This paper is the basis for Primo's SAT backend interface.

Primo uses IPASIR-UP to avoid implementing a custom SAT solver while preserving a clean integration point for lazy DPLL(T), and later tighter CDCL(T)-style interaction.

## What we use initially

- incremental SAT backend interface
- adding clauses
- assumptions
- solving under assumptions
- assignment/model-value queries
- failed-assumption information where available
- CaDiCaL as the initial backend implementation

## Deferred

- user propagator callbacks
- inspecting trail changes during search
- adding theory propagations during search
- adding clauses during search
- theory-guided decisions
- explaining user propagations

## Boundaries

- Do not implement custom DPLL or CDCL in Primo.
- Do not copy CaDiCaL code.
- Keep CaDiCaL-specific integration behind the SAT backend adapter.
- Keep SMT terms and theory state out of backend-specific code.

## Tests derived from the interface

- add simple clauses and solve
- solve under assumptions
- distinguish permanent clauses from assumptions
- query assignment only after SAT
- report missing backend dependency clearly
```

## 19.2 `docs/papers/cvc4-architecture.md`

```markdown
# CVC4 architecture

## Citation

Clark W. Barrett, Christopher L. Conway, Morgan Deters, Liana Hadarean, Dejan Jovanovic, Tim King, Andrew Reynolds, Cesare Tinelli:  
CVC4. CAV 2011.

## Role in Primo

This paper is used as an architectural reference, not as a direct algorithmic specification.

## What we use

- modular SMT engine
- typed terms
- theory solvers
- SMT-LIB orientation
- model construction
- incremental solving as a design concern

## What we do not implement initially

- full SMT-LIB
- quantifiers
- arrays
- bit-vectors
- datatypes
- strings
- proof production

## Implementation notes

Primo should keep clear boundaries between:

- SMT-LIB frontend
- core term representation
- SAT layer
- SMT/DPLL(T) layer
- theory solvers
- model construction

## Milestones

- M0: core term and SMT-LIB scaffold
- M1: Boolean abstraction and IPASIR-UP/CaDiCaL SAT interface
- M2: lazy DPLL(T)
- M3: QF_UF theory solver
```

## 19.3 `docs/papers/fast-congruence-closure.md`

```markdown
# Fast congruence closure and extensions

## Citation

Robert Nieuwenhuis, Albert Oliveras:  
Fast Congruence Closure and Extensions.

## Role in Primo

This paper is the basis for the QF_UF theory solver.

## What we use

- ground equality reasoning
- congruence closure
- equality propagation concepts
- explanation-producing congruence closure, later

## Initial implemented subset

- union-find over terms
- congruence closure for applications
- equality assertions
- disequality assertions
- disequality conflict detection

## Deferred

- optimized incremental backtracking
- theory propagation
- full explanation machinery
- proof production

## Data structures

TODO:

- equivalence classes
- application lookup/signature table
- pending merge queue
- disequality set
- explanation graph

## Invariants

TODO.

## Tests derived from the paper

- equality/disequality contradiction
- unary congruence
- binary congruence
- nested congruence
```

## 19.4 `docs/papers/fast-linear-arithmetic-dpllt.md`

```markdown
# A Fast Linear-Arithmetic Solver for DPLL(T)

## Citation

Bruno Dutertre, Leonardo de Moura:  
A Fast Linear-Arithmetic Solver for DPLL(T).

## Role in Primo

This paper is the later basis for LRA.

## What we use later

- Simplex-style arithmetic solver
- bounds
- tableau
- DPLL(T) integration
- backtracking
- theory propagation
- strict inequalities

## Not implemented initially

The initial Primo scaffold does not implement full LRA.

## Future data structures

- rational coefficients
- linear polynomials
- bounds
- tableau rows
- arithmetic trail
- explanation objects

## Milestones

- LRA0: linear polynomial representation
- LRA1: normalization of arithmetic atoms
- LRA2: tableau scaffold
- LRA3: feasibility checking
- LRA4: conflict explanations
- LRA5: backtracking and propagation
```

## 19.5 `docs/papers/difference-constraint-propagation.md`

````markdown
# Fast and Flexible Difference Constraint Propagation for DPLL(T)

## Citation

Scott Cotton, Oded Maler:  
Fast and Flexible Difference Constraint Propagation for DPLL(T).

## Role in Primo

This paper is the later basis for difference-logic propagation.

## What we use later

- constraints of form `x - y <= c`
- weighted graph representation
- negative-cycle conflict detection
- propagation of implied bounds

## Initial representation

```text
x - y <= c
means
x <= y + c

Graph edge:
y -> x with weight c
```

## Not implemented initially

The initial Primo scaffold does not need a full difference-logic solver.

## Future milestones

* DL0: parse difference constraints
* DL1: graph construction
* DL2: negative-cycle detection
* DL3: conflict explanation from cycle edges
* DL4: model via potentials
* DL5: flexible propagation

````

---

# 20. TODO file

Create `TODO.md`.

```markdown
# TODO

## M0: Core architecture

- [ ] Create typed sort representation.
- [ ] Create symbol table.
- [ ] Create hash-consed term manager.
- [ ] Create SMT-LIB parser for QF_UF subset.
- [ ] Create pretty-printer.
- [ ] Add round-trip parser tests.
- [ ] Add sort-checking tests.

## M1: SAT and abstraction

- [ ] Define SAT literals, variables, and clauses.
- [ ] Define IPASIR-UP SAT backend interface.
- [ ] Add CaDiCaL-backed IPASIR-UP adapter.
- [ ] Add clear dependency detection and error messages for missing CaDiCaL/IPASIR-UP.
- [ ] Implement Boolean abstraction from Bool terms to SAT clauses.
- [ ] Maintain atom-to-SAT-literal mapping.
- [ ] Add tests for abstraction polarity.

## M2: Lazy DPLL(T)

- [ ] Define theory-solver interface.
- [ ] Implement lazy DPLL(T) loop.
- [ ] Extract true theory literals from SAT assignment.
- [ ] Add theory conflict lemmas back to SAT solver.
- [ ] Add smoke tests comparing with Z3.

## M3: QF_UF congruence closure

- [ ] Implement union-find over TermId.
- [ ] Register application terms.
- [ ] Merge equality classes.
- [ ] Maintain congruence closure.
- [ ] Support disequality constraints.
- [ ] Detect equality/disequality conflicts.
- [ ] Add QF_UF regression tests.

## M4: EUF explanations

- [ ] Track source literal for every asserted equality/disequality.
- [ ] Explain equality paths.
- [ ] Explain congruence-derived equalities.
- [ ] Generate conflict clauses.
- [ ] Validate conflict lemmas on regression tests.

## M5: QF_UF models

- [ ] Build equivalence classes as domain elements.
- [ ] Interpret constants.
- [ ] Interpret functions on observed tuples.
- [ ] Add default function values.
- [ ] Print SMT-LIB-style model.
- [ ] Validate simple models with Z3/cvc5 where possible.

## M6: Difference logic

- [ ] Parse constraints of form `x - y <= c`.
- [ ] Normalize difference constraints.
- [ ] Build weighted graph.
- [ ] Detect negative cycles.
- [ ] Explain cycle conflicts.
- [ ] Add QF_IDL/QF_RDL tests.

## M7: LRA scaffold

- [ ] Represent rational coefficients.
- [ ] Represent linear polynomials.
- [ ] Normalize arithmetic atoms.
- [ ] Define bounds and tableau rows.
- [ ] Add parser/type-checker tests.

## M8: LRA solver

- [ ] Implement feasibility checking.
- [ ] Implement pivoting.
- [ ] Implement conflict explanations.
- [ ] Add backtracking.
- [ ] Add theory propagation.
```

---

# 21. CMake and minimal build/test commands

Create a root `CMakeLists.txt`.

Minimum CMake requirements:

```text
cmake_minimum_required(VERSION 3.20)
project(primo LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()
```

The CMake scaffold should:

```text
build a `primo` executable
keep source files under `src/`
support unit tests through CTest when tests exist
detect or clearly report missing CaDiCaL/IPASIR-UP dependencies
avoid vendoring CaDiCaL by default
```

Create a `Makefile` or equivalent task file with these targets, even if some are initially placeholders.

```text
make build
make test
make smoke
make compare
make fuzz
make bench
make docs
make clean
```

Expected behavior:

```text
make build:
  configures CMake and builds Primo

make test:
  runs unit tests

make smoke:
  runs Primo on benchmarks/smoke

make compare:
  compares Primo against Z3/cvc5 if installed

make fuzz:
  runs a short external-fuzzer or internal-fuzzer smoke test if available

make bench:
  runs benchmark script on smoke tests

make docs:
  builds the MyST/Jupyter Book documentation site
  call `scripts/build_docs.sh`
  create `.venv-docs` when needed
  build static HTML under `docs/_build/html`

make clean:
  removes local build artifacts and results
```

If a dependency is missing, print a clear message and skip gracefully where appropriate.

---

# 22. Initial Codex execution plan

When creating the scaffold, proceed in this order:

```text
1. Create repository files:
   README.md
   AGENTS.md
   DESIGN.md
   TODO.md

2. Create docs:
   docs/_config.yml
   docs/_toc.yml
   docs/index.md
   docs/architecture.md
   docs/sat-ipasir-up.md
   docs/dpllt.md
   docs/euf.md
   docs/arithmetic.md
   docs/difference_logic.md
   docs/debugging.md
   docs/literature.md
   docs/reference/llm2smt-retrospective.md
   docs/papers/*.md

3. Create skills:
   skills/code-review/SKILL.md
   skills/testing/SKILL.md
   skills/fuzzing/SKILL.md
   skills/benchmarking/SKILL.md
   skills/documentation/SKILL.md
   skills/literature-survey/SKILL.md
   skills/performance/SKILL.md
   skills/delta-debugging/SKILL.md

4. Create source tree directories.

5. Create minimal source files or placeholders.

6. Create smoke benchmarks.

7. Create scripts:
   run_smtlib.py
   compare_with_z3.py
   fuzz_external.py
   minimize_smt2.py
   bench.py
   validate_model.py

8. Create build/test runner.

9. Run the available tests or smoke commands.

10. Report:
    - files created
    - commands run
    - commands that are placeholders
    - next recommended implementation step
```

---

# 23. Important constraints

Do not:

```text
copy code from LLM2SMT
copy code from other SMT solvers
copy parser grammars from other solvers
claim unimplemented features are supported
implement full arithmetic before QF_UF is scaffolded
optimize before tests and benchmarks exist
hide unsupported inputs as unknown
```

Do:

```text
keep the project buildable
keep scripts runnable or gracefully skipped
write clear TODOs
document paper basis
add tiny smoke tests
make unsupported fragments explicit
prefer small modules
prefer deterministic behavior
```

---

# 24. Expected final report from Codex

After creating the scaffold, report in this format:

````markdown
# Primo scaffold created

## Files created

...

## Implemented functionality

...

## Placeholders

...

## Commands run

```bash
...
```

## Results

...

## Missing dependencies

...

## Recommended next task

Implement M0: typed sort representation, symbol table, and hash-consed term manager.

````

Do not proceed to large algorithmic implementation unless explicitly asked.
