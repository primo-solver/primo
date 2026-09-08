from __future__ import annotations

import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).parents[2] / "scripts/slurm_qf_uflia_mutation_fuzz_gate.sbatch"

# Execute the actual batch script without a scheduler or solver dependencies.
# The mutation stub retains one input so the model-validation phase must run.
STUB = r"""
import json
import os
from pathlib import Path
import sys

name = Path(sys.argv[0]).name
args = sys.argv[1:]
root = Path(os.environ["PRIMO_WORKDIR"])
if name == "yinyang_fuzz.sh":
    (root / "answer-args.json").write_text(json.dumps(args))
    scratch = Path(args[args.index("--scratch") + 1])
    scratch.mkdir(parents=True)
    (scratch / "mutant.smt2").write_text("(set-logic QF_UFLIA)\n(check-sat)\n")
elif name == "python3":
    assert args[0] == "scripts/validate_model.py", args
    (root / "model-args.json").write_text(json.dumps(args[1:]))
    sys.exit(int(os.environ.get("TEST_MODEL_EXIT", "0")))
"""


class MutationGateOptionsTests(unittest.TestCase):
    def run_gate(
        self, options: str | None, model_exit: int = 0
    ) -> tuple[list[str], list[str]]:
        with tempfile.TemporaryDirectory(prefix="primo-mutation-gate-") as directory:
            root = Path(directory)
            for relative in (
                "build-slurm-rel/primo",
                "scripts/yinyang_fuzz.sh",
                "contrib/local/bin/typefuzz",
                "contrib/local/bin/z3",
                "contrib/local/bin/dolmen",
                "contrib/local/bin/python3",
            ):
                executable = root / relative
                executable.parent.mkdir(parents=True, exist_ok=True)
                executable.write_text(f"#!{sys.executable}\n" + STUB)
                executable.chmod(0o755)
            seeds = root / "seeds"
            seeds.mkdir()
            env = {
                key: value
                for key, value in os.environ.items()
                if not key.startswith("PRIMO_")
            }
            env.update(
                PRIMO_WORKDIR=str(root),
                PRIMO_FUZZ_YINYANG_SEEDS=str(seeds),
                PRIMO_FUZZ_OUT_ROOT=str(root / "out"),
                TEST_MODEL_EXIT=str(model_exit),
            )
            if options is not None:
                env["PRIMO_FUZZ_SOLVER_ARGS"] = options
            result = subprocess.run(
                ["bash", str(SCRIPT)],
                env=env,
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.assertEqual(
                result.returncode, model_exit, result.stdout + result.stderr
            )
            answer = json.loads((root / "answer-args.json").read_text())
            self.assertTrue(
                answer[answer.index("--config") + 1].endswith(
                    "benchmarks/fuzz-configs/qf_lia_typefuzz.txt"
                )
            )
            model = json.loads((root / "model-args.json").read_text())
            command = shlex.split(answer[answer.index("--solver") + 1])
            self.assertEqual(command[0], model[model.index("--solver") + 1])
            self.assertIn("--require-dolmen-pass", model)
            self.assertTrue(model[-1].endswith("/scratch/mutant.smt2"))
            model_options = [
                arg.removeprefix("--solver-arg=")
                for arg in model
                if arg.startswith("--solver-arg=")
            ]
            return command[1:], model_options

    def test_both_phases_receive_identical_options(self) -> None:
        for options in (
            None,
            "",
            "--no-qf-uflia-model-combination",
            "  --no-qf-uflia-model-combination\t--preprocessing-mode=strong  "
            "--max-theory-conflicts 50 ",
        ):
            with self.subTest(options=options):
                answer, model = self.run_gate(options)
                expected = ["--quiet", *shlex.split(options or "")]
                self.assertEqual(answer, expected)
                self.assertEqual(model, expected)

    def test_model_validation_failure_is_propagated(self) -> None:
        answer, model = self.run_gate("--no-qf-uflia-model-combination", model_exit=1)
        self.assertEqual(model, answer)


if __name__ == "__main__":
    unittest.main()
