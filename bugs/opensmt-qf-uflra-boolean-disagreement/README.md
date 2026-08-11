# OpenSMT QF_UFLRA Boolean Disagreement

## Status

Reference-solver discrepancy. This is not currently classified as a Primo
wrong answer: Primo and Z3 agree, while OpenSMT reports a different result.

## Reproduction

From the repository root:

```bash
contrib/local/bin/z3 bugs/opensmt-qf-uflra-boolean-disagreement/witness.smt2
./build/primo bugs/opensmt-qf-uflra-boolean-disagreement/witness.smt2
contrib/local/bin/opensmt bugs/opensmt-qf-uflra-boolean-disagreement/witness.smt2
contrib/local/bin/cvc5 bugs/opensmt-qf-uflra-boolean-disagreement/witness.smt2
contrib/local/bin/yices-smt2 bugs/opensmt-qf-uflra-boolean-disagreement/witness.smt2
```

Expected observed results with the recorded local binaries:

```text
z3      unsat
primo   unsat
opensmt sat
cvc5    unsat
yices   unsat
```

The witness was delta-minimized from one of the 20 OpenSMT disagreements found
in a 100-case recursive Boolean QF_UFLRA probe (seed `20260822`, timeout five
seconds).
The full probe is recorded in `docs/run-log.json` as
`2026-08-07-qf-uflra-opensmt-reference-probe`.

## Shape

The minimized input contains only:

- two clauses forcing `a = b`;
- a disequality between `f(a)` and `f(b)`; and
- one independent QF_LRA constraint to retain the mixed logic.

The same input is accepted by Primo and Z3 and is classified `unsat` by both.
OpenSMT's result should therefore be independently triaged before OpenSMT is
used as an authoritative oracle for this Boolean QF_UFLRA fragment.
