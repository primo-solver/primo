"""Model replay must preserve the effective options used by each fuzz corpus."""

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class ModelReplayTests(unittest.TestCase):
    def test_generation_and_replay_use_matching_per_corpus_options(self):
        for requested, mutate in (
            ("--qf-uflra-online-euf", False),
            ("--qf-uflra-online-euf --preprocessing-mode=off", False),
            ("--qf-uflra-online-euf", True),
        ):
            with (
                self.subTest(requested=requested),
                tempfile.TemporaryDirectory() as directory,
            ):
                root = Path(directory)
                tools = root / "contrib/local/bin"
                tools.mkdir(parents=True)
                fake = tools / "python3"
                # Fakes record workflow arguments and create manifests; no solver runs.
                fake.write_text(
                    f"#!{sys.executable}\n"
                    + """import json
import os
import sys
from pathlib import Path
args = sys.argv[1:]
if os.environ.get('MUTATE_SOLVER') == '1' and args[0].endswith('qf_uflra_smoke_fuzz.py'):
    with (Path(os.environ['PRIMO_WORKDIR']) / 'build-slurm-rel/primo').open('a') as stream:
        stream.write('# modified after fingerprint\\n')
with Path(os.environ['COMMAND_LOG']).open('a') as stream:
    stream.write(json.dumps(args) + '\\n')
if args[0].endswith('select_fuzz_sat_inputs.py'):
    destination = Path(args[args.index('--out') + 1])
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('fixture.smt2\\n')
elif '--out' in args:
    Path(args[args.index('--out') + 1]).mkdir(parents=True, exist_ok=True)
"""
                )
                fake.chmod(0o755)
                for tool in ("z3", "cvc5", "dolmen"):
                    executable = tools / tool
                    executable.write_text("#!/bin/sh\nexit 0\n")
                    executable.chmod(0o755)
                solver = root / "build-slurm-rel/primo"
                solver.parent.mkdir()
                solver.write_text("#!/bin/sh\nprintf 'primo\\ngit: fixture\\n'\n")
                solver.chmod(0o755)
                log = root / "calls.jsonl"
                env = {
                    **os.environ,
                    "COMMAND_LOG": str(log),
                    "SLURM_JOB_ID": "fixture",
                    "PRIMO_EXPECTED_REVISION": "fixture",
                    "PRIMO_WORKDIR": str(root),
                    "MUTATE_SOLVER": "1" if mutate else "0",
                    "PRIMO_FUZZ_SOLVER_ARGS": requested,
                    "PRIMO_FUZZ_OUT_DIR": str(root / "out"),
                }
                result = subprocess.run(
                    ["bash", str(ROOT / "scripts/slurm_qf_uflra_fuzz_gate.sbatch")],
                    env=env,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(result.returncode, int(mutate), result.stderr)
                task = root / "out/task-0"
                self.assertEqual(
                    (task / "exit-code").read_text().strip(), str(int(mutate))
                )
                self.assertEqual(
                    (task / "binary-before.sha256").read_text()
                    == (task / "binary-after.sha256").read_text(),
                    not mutate,
                )
                calls = [json.loads(line) for line in log.read_text().splitlines()]
                generation = {
                    Path(args[args.index("--out") + 1]).name: args
                    for args in calls
                    if args[0].endswith(
                        ("qf_uflra_smoke_fuzz.py", "finite_domain_fuzz.py")
                    )
                }
                replay = {
                    Path(args[args.index("--out") + 1]).name: args
                    for args in calls
                    if args[0].endswith("validate_model.py")
                }
                self.assertEqual(
                    set(generation), {"general", "finite-qf-uf", "finite-qf-uflra"}
                )
                self.assertEqual(set(replay), set(generation))
                for corpus, args in replay.items():
                    self.assertIn("--require-dolmen-pass", args)
                    # Replay may quiet output, while solving options must be identical.
                    actual = [
                        arg
                        for arg in args
                        if arg.startswith("--solver-arg=")
                        and arg != "--solver-arg=--quiet"
                    ]
                    expected = [
                        arg
                        for arg in generation[corpus]
                        if arg.startswith("--solver-arg=")
                        and arg != "--solver-arg=--quiet"
                    ]
                    self.assertEqual(actual, expected, corpus)


if __name__ == "__main__":
    unittest.main()
