# Tuning an existing configuration

Start from a qualified baseline and a hypothesis: which mechanism should change,
on which inputs, and what observation would reject it? The
[policy reference](../reference/policies.md) identifies option dependencies.

1. Check the parameter's actual callers and effective route. Compare counters
   at two admissible values before adding it to a search space.
2. Keep training and evaluation inputs separate. Record the space, seed,
   objective, limits and optimizer version; budget both optimizer overhead and
   solver calls.
3. Use the existing {source}`irace <scripts/irace/target_runner.py>` or
   {source}`SMAC <scripts/smac/qfuf_gap_tune.py>` wrappers only for their supported spaces.
   A QF_UF parameter file is not an LRA configuration.
4. Re-evaluate the selected configuration against the unchanged baseline on
   the stated full evaluation set. Report family losses and unique solves as
   well as aggregate solved/PAR2 and paired times.
5. Run fresh configuration-specific fuzzing and separate SAT-model checks
   before accepting a default or headline performance claim.

An inactive parameter consumes search budget without testing a mechanism.
An average loss can still justify a portfolio lead if unique solves compensate
for its scheduling cost, but it does not justify a new default. Record the
accepted or rejected decision and all provenance through
[the experiment workflow](experiments.md).

The {source}`historical parameter inventory <docs/tuning-parameters.md>` and
{source}`complementarity investigation <docs/open-leads.md#L2572-L2632>` retain
previous motivations and negative results. Recheck their preconditions instead
of treating the old candidate list as a current recommendation.
