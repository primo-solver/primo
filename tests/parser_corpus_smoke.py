"""Check production-parser byte offsets, EOF traversal and scope diagnostics."""

import json
import subprocess
import sys
import tempfile
from pathlib import Path

with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / "input.smt2"
    prefix = b'(set-info :source "(check-sat)")(push 1)(declare-const |a)| Bool)(assert |a)|)(check-sat)'
    suffix = b" ; ignored (check-sat)\n(pop 1)(check-sat)"
    path.write_bytes(prefix + suffix)
    result = subprocess.run([sys.argv[1], str(path)], capture_output=True, check=True, timeout=5)
    report = json.loads(result.stdout)
    assert report["query_ends"] == [len(prefix), len(prefix + suffix)]
    assert report["assertions"] == report["declarations"] == report["pushes"] == report["pops"] == 1
    assert report["final_depth"] == 0 and report["max_depth"] == 1
    for text in [b"(pop 1)", b"(assert true", b"(check-sat-assuming ())"]:
        path.write_bytes(text)
        assert subprocess.run([sys.argv[1], str(path)], capture_output=True,
                              timeout=5, check=False).returncode == 2
