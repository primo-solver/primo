# Evidence work still open

These are unresolved provenance or checking tasks, not undocumented solver
algorithms. The original records remain available and retain their limitations.

| Task | What is needed to close it |
| --- | --- |
| Published QF_LRA tables | Identify exact Primo commits, manifests, commands, and raw artifacts for Tables 1–2 in the September 2026 paper; verify every aggregate against the ledger. The [report](../papers/primo-qflra-results.md) currently supplies publication-level results only. |
| Quantifier reference-less UNSAT answers | Resolve the nine unconfirmed answers in the historical pool-check record, or retain them as explicitly unconfirmed. Do not infer confirmation from a later unrelated gate. |
| Quantifier export trust | Independently check the conservative-extension conditions of witness exports if proof-like validation is required. The current checker trusts that export contract; see [the checking argument](../evidence/quantifiers.md). |
| Historical records with missing metadata | Preserve the original evidence status and name the missing command, revision or artifact. Do not backfill guesses or count them as qualification. |

Results-storage migration is [separate work](results-storage.md). Changing the
storage format would not supply missing experiment identities or strengthen an
incomplete correctness check.
