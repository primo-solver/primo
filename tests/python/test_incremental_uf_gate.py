import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from incremental_uf_gate import generated, scoped
from native_uf_sessions import classify, query_metadata, run_session
import native_uf_sessions
from validate_incremental import QueryJournal, command_spans


class UfGateTests(unittest.TestCase):
    def test_direct_child_resources_exit_and_timeout(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "input.smt2"
            source.write_text("(check-sat)")
            for index, program in enumerate((
                    "print('sat'); raise SystemExit(3)",
                    "import time; print('sat', flush=True); time.sleep(10)")):
                out = root / str(index)
                out.mkdir()
                code, timed_out, seconds = run_session(
                    [sys.executable, "-c", program], source, out, 0.1)
                self.assertEqual(timed_out, index == 1)
                self.assertEqual(code, None if timed_out else 3)
                self.assertGreater(int((out / "maxrss-kib").read_text()), 0)
                self.assertLess(seconds, 2)
                self.assertEqual((out / "stdout").read_text().strip(), "sat")

    def test_reference_passing_replay_cannot_change_timed_answer(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = root / "tool"
            binary.write_bytes(b"mock tool")
            source = root / "case.smt2"
            source.write_text("(set-logic QF_UF)(check-sat)")
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({"entries": [{
                "file": source.name, "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                "queries": 1, "answers": ["unknown"]}]}))
            timing = root / "timing" / "session-0000"
            timing.mkdir(parents=True)
            (timing / "result.json").write_text(json.dumps({
                "outcome": "complete", "answers": ["unsat"]}))
            arguments = ["native_uf_sessions.py", "validate", "--root", str(root),
                         "--manifest", str(manifest), "--solver", str(binary),
                         "--ref", str(binary), "--dolmen", str(binary),
                         "--timing-out", str(root / "timing"), "--out", str(root / "out")]
            with (mock.patch.object(sys, "argv", arguments),
                  mock.patch.object(native_uf_sessions.subprocess, "check_output", return_value="v"),
                  mock.patch.object(native_uf_sessions, "validate", return_value={
                      "outcome": "pass", "queries": [{"answer": "sat"}]}),
                  mock.patch("builtins.print")):
                self.assertEqual(native_uf_sessions.main(), 1)
            result = json.loads((root / "out/session-0000/result.json").read_text())
            self.assertEqual(result["outcome"], "failure")
            self.assertIn("timed query answers", result["message"])

    def test_only_fresh_command_status_is_an_oracle(self):
        metadata = query_metadata('''(set-logic QF_UF)
(set-info :source "(set-info :status unsat)")
(set-info :status sat) (declare-const p Bool) (check-sat)
(push 2) (assert p) (check-sat) (pop 2)
(set-info :status unsat) (reset-assertions) (check-sat)''')
        self.assertEqual(metadata, {"answers": ["sat", "unknown", "unknown"],
                                    "queries": 3, "push": 2, "pop": 2})

    def test_generated_scopes_and_reproducibility(self):
        for seed in range(30):
            text = generated(seed)
            self.assertEqual(text, generated(seed))
            journal = QueryJournal()
            queries = 0
            for start, end, head in command_spans(text):
                journal.apply(text[start:end], head)
                queries += head == ("check-sat",)
            self.assertEqual(queries, 9)
            self.assertEqual(len(journal.frames), 1)

    def test_mutation_does_not_reuse_popped_assertions(self):
        text = """(set-logic QF_UF)
(push 1) (declare-const hidden Bool) (assert hidden) (pop 1) (check-sat)"""
        output = scoped([(text[a:b], h) for a, b, h in command_spans(text)], 0, True)
        self.assertNotIn("hidden", output.split("(pop 1)", 1)[1])

    def test_timeout_and_exit_override_partial_answers(self):
        self.assertEqual(classify("sat\n", 0, True, ["sat"])["outcome"], "timeout")
        self.assertEqual(classify("sat\n", 1, False, ["sat"])["outcome"], "error")
        self.assertEqual(classify("sat\n", 0, False, ["unknown"])["outcome"], "complete")
        self.assertEqual(classify("unsat\n", 0, True, ["sat"])["outcome"], "wrong-answer")
        self.assertEqual(classify("sat\nsat\n", 0, False, ["sat"])["outcome"], "error")


if __name__ == "__main__":
    unittest.main()
