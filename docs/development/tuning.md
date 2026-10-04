# Tuning an existing configuration

Start from a qualified baseline and a hypothesis: which mechanism should change,
on which inputs, and what observation would reject it? The
[policy reference](../reference/policies.md) identifies option dependencies.

1. Check the parameter's actual callers and effective route. Compare counters
   at two admissible values before adding it to a search space.
2. Keep training and evaluation inputs separate. Record the space, seed,
   objective, limits and optimizer version; budget both optimizer overhead and
   solver calls.
3. Use the existing [irace](https://github.com/primo-solver/primo/blob/a19493701d7ab31931d7466fd88a44803f41abf3/scripts/irace/target_runner.py) or
   [SMAC](https://github.com/primo-solver/primo/blob/a19493701d7ab31931d7466fd88a44803f41abf3/scripts/smac/qfuf_gap_tune.py) wrappers only for their supported spaces.
   A QF_UF parameter file is not an LRA configuration.
4. Re-evaluate the selected configuration against the unchanged baseline on
   the stated full evaluation set. Report family losses and unique solves as
   well as aggregate solved/PAR2 and paired times.
5. Run fresh configuration-specific fuzzing and separate SAT-model checks
   before accepting a default or headline performance claim.

Both wrappers check timeouts and process exit status before accepting an answer.
A timeout, signal termination, or nonzero exit costs twice the timeout (PAR2),
even if the process printed `sat` or `unsat` first. A normally completed wrong
answer retains the larger wrong-answer penalty. SMAC records stdout, stderr
and exit status in its target and validation CSV files. Set `PRIMO_TUNE_LOG`
for irace to retain exit status and a JSON-escaped output comment after each
run record. Timeout records use synthetic exit code `124`; other runs retain
the observed process return code. Historical tuning records are unchanged; exclude abnormal exits
when interpreting older scores.

The [B-014 repair](../coverage/page-review.md#supporting-tools-and-resource-controls)
has mocked-process regressions for post-answer errors, signal termination,
timeouts and normally completed wrong answers. These tests do not qualify a
solver configuration or replace the SLURM evaluation gates.

An inactive parameter consumes search budget without testing a mechanism.
An average loss can still justify a portfolio lead if unique solves compensate
for its scheduling cost, but it does not justify a new default. Record the
accepted or rejected decision and all provenance through
[the experiment workflow](experiments.md).

The {source}`historical parameter inventory <docs/tuning-parameters.md>` and
{source}`complementarity investigation <docs/open-leads.md#L2583-L2642>` retain
previous motivations and negative results. Recheck their preconditions instead
of treating the old candidate list as a current recommendation.
