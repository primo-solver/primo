# Static-analysis triage, 27 October 2026

The [first local audit](../development/static-analysis.md) recorded 745 distinct
diagnostics against solver source `88ca313`. This review assigns a disposition
to every original row in the
{diagnosticsource}`machine-readable triage record <docs/coverage/local-static-triage-20261027.json>`. A disposition can
be a guarded case, an intentional contract, a cleanup candidate or an open
follow-up; it does not mean that every warning has been removed or that the
solver has a complete lifetime or exception-safety proof.

## Confirmed rollback allocation defect

Tracing the `ModelScope` exception warning led to
{rollbacksource}`FlatCongruenceClosure::pop <src/theory/equality/flat_congruence_closure.cpp>`.
It resized three representative-proof arrays to the saved node count in every
mode. `ensure_node_count` allocated those arrays only for the representative
`proof-forest` mode or explicit proof diagnostics. A first pop could therefore
allocate unused arrays during BFS or the separate reoriented/structural modes,
including a pop invoked by `ModelScope` destruction.

The repair applies the same mode condition during pop. On a 64-bit build the
three arrays previously added 24 bytes of unused element payload per saved
node, before allocator overhead. This is a storage calculation, not a measured
RSS or timing improvement. The
{rollbacksource}`regression <tests/unit/euf_solver_tests.cpp>` exercises nested
checkpoints and post-checkpoint nodes across all five explanation modes, with
diagnostics both on and off. Disabled representative-proof capacities must
stay zero, and equality rollback must remain correct. The new EUF test
translation unit aborts when linked to the preserved pre-fix libraries; it
passes against the repaired release libraries.

`ModelScope` still treats a violated checkpoint invariant as fatal through its
destructor and `noexcept` move assignment. The repair removes this particular
allocation path; a complete exception-guarantee review with allocation-failure
injection remains open. No solver option or search heuristic changes here.

## Dispositions of the original inventory

| Diagnostics | Count | Review result |
| --- | ---: | --- |
| Include-cleaner | 409 | 20 paired self-includes retained; 389 direct-provider suggestions remain dependency hygiene work |
| Optional access | 109 | 37 production accesses have local or caller/factory engagement invariants; 72 test accesses depend on assertions or fixture setup |
| Enum size and record padding | 82 | Layout candidates; measure containing records and allocation frequency before changing types |
| Easily swappable parameters | 40 | API readability risks; no swapped call demonstrated by these warnings |
| Test exception boundaries, enum casts, assertion side effects and multiplication | 36 | Test failure behavior or bounded/explicitly enabled setup; no production overflow or discarded release setup found |
| Identical switch branches | 5 | Inspected alternatives intentionally have the same result |
| Value/copy/move and optional wrapping | 44 | Ownership boundaries, harmless moves, copy-only receiving interfaces and verified-cleanup candidates are distinguished per row |
| Vector reserve | 6 | Potential allocation cleanup; three sites are test-only |
| String operations | 11 | Test-only construction/search cleanup |
| Production cleanup exception warnings | 2 | Broader exception contract remains a follow-up after the allocation repair |
| Dependency leak warning | 1 | CLI11 `std::function` capture trace; no runtime leak established |

For example, the LRA model mutation warning at the original line 1671 concerns
`saved = current`: `current` is already an owning immutable snapshot, so the
second copy is a cleanup candidate. Keeping the original snapshot matters
because `apply` changes the assignment vector. Likewise, five const payloads
passed through `std::move` still copy vector/model data. These merit a focused
verified cleanup rather than a blanket conversion of values to references.

The optional-access dispositions state their preconditions individually. Fixture-only
unchecked `operator*`/`operator->` accesses need explicit presence checks to
guarantee a clean failure when an assumed fixture condition breaks. A missing
value otherwise violates the optional observer precondition and can cause
undefined behavior. The [standard observer contract](https://eel.is/c++draft/optional.observe)
distinguishes that from `value()`, which throws on absence. These are test
hardening follow-ups, not production fallback policies. Enum width changes also require
care around existing bitmask/index types and public option layouts; a smaller
enum alone does not establish a smaller containing record.

## Make redundant includes fail

Before the first local audit, the default clang-tidy invocation in
[the Makefile at `88ca313`](https://github.com/primo-solver/primo/blob/88ca31342d15c1e8bdebef176cd6b2d7b15daaec/Makefile#L121-L140)
did not enable include-cleaner. Its separate include scan was advisory and
could skip a missing tool successfully. The first audit's `4dca989` tooling
already enabled the broad profile and made missing tools fatal; findings still
needed triage and an explicit acceptance gate. Tool completion was being
confused with review acceptance.

The [local workflow](../development/static-analysis.md) now keeps missing
analyzers fatal and makes both `make tidy` and `make include-cleaner` fail on an
unused include, except an implementation's own `.hpp`/`.h` header. That
exception follows source-relative quoted lookup and the configured
`-iquote`, `-I`, `-isystem` and `-idirafter` order for every compile context.
Unsupported search configurations conservatively receive no exemption. A
source-relative or earlier search-path shadow is rejected, as is an unrelated
matching basename. All 20 current unused-header reports are one-line paired
header anchors, which preserve standalone header compilation. The gate records
them explicitly. Other deliberate includes can use a reviewed LLVM-supported
`IWYU pragma: keep`; no such suppression was added by this review.

Missing direct includes remain visible as advisory findings. Of 389 rows, the
tool supplies 225 insertion proposals. Those proposals are retained in the
triage data; owning/public providers and layer boundaries still need review
before applying a cleanup batch. A synthetic real-clang probe verifies that an
unused extra header fails the gate and using its declared type passes, while
the paired header remains retained.

The [LLVM include-cleaner documentation](https://releases.llvm.org/18.1.8/tools/clang/tools/extra/docs/clang-tidy/checks/misc/include-cleaner.html)
describes the unused/missing distinction; LLVM's
[include-cleaner design](https://clangd.llvm.org/design/include-cleaner) documents
intentional include retention.

## Evidence and remaining work

The {diagnosticsource}`qualification report <docs/coverage/static-diagnostic-repair-qualification-20261027.json>`
and {diagnosticsource}`run ledger <docs/run-log.json>` record 4,200 generated
inputs/sessions, 8,086 direct Dolmen model checks, 45/45 release and 45/45
ASan/LSan native tests, and 284 Python tests on the frozen native snapshot.
The final changed tooling separately passes 26 static-workflow tests plus the
full-driver mocked replay test. The fresh static pass covers 132 translation
units and retains 745 visible diagnostics, with zero redundant-include blockers.
The source repair is qualified separately from the metadata gate. Release,
ASan, differential fuzzing, explicit online-EUF checking and a fresh whole-source
static pass are recorded in the run ledger. The initial
online-EUF job stopped before generating inputs because its reused tool prefix
lacked cvc5; retries use an existing verified cvc5 binary in fresh output
directories. That setup failure contributes no solver evidence. Model replay
also needed to cover finite QF_UF and preserve its forced strong preprocessing.
Finite QF_UFLRA uses the requested profile. The corrected driver shares
per-corpus option vectors between generation and replay; its mocked regression
compares both call sets, including a requested preprocessing override. The
accepted run's metadata agrees on those options. It records and compares the
solver's SHA-256 before and after execution, captures its version and reference
binary hashes, and rejects reuse of an existing task output directory. Intermediate replay
attempts with omitted or changed options are retained as provisional evidence.

The CLI11 warning traces a copied callback into App-owned option state.
A separate ASan/LSan CLI construction/destruction probe checks the exercised
help/version path; it is not proof that every dependency path is leak-free.
The remaining priorities are the checkpoint exception contract, direct-header
hygiene and avoidable const-payload copies. No performance-profile promotion
or full-corpus performance conclusion follows from this review.
