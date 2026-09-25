"""Record peak child-process RSS for a sequential benchmark harness."""
import resource
import subprocess
import sys
from pathlib import Path

result = subprocess.run(sys.argv[2:], check=False)
Path(sys.argv[1]).write_text(f"peak_rss_kb={resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss}\n")
raise SystemExit(result.returncode)
