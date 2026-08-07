# QF_UFLRA Performance Regressions

This directory contains source benchmarks retained for performance diagnosis.
They are not mutation seeds and are not part of the bounded pre-mutation seed
gate.

The two MathSAT random instances exposed an unsound shortcut that skipped the
final LRA implied-equality scan after paired-bound online propagation. The
soundness requirement is covered by the compact
`benchmarks/seeds/qf_uflra/online_nonpaired_congruence_forced_unsat.smt2`
witness and focused QF_UFLRA unit tests. These full instances remain useful for
measuring search behavior after the convex Nelson-Oppen equality-sharing path
changes, but their runtime must not prevent YinYang from starting.

Run both files through the standard comparison harness using
`PRIMO_COMPARE_ROOT=benchmarks/performance/qf_uflra` and
`PRIMO_COMPARE_MANIFEST=benchmarks/qf_uflra-deferred-performance-regressions.txt`.
