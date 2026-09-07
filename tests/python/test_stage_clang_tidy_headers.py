import importlib.util
from pathlib import Path
import unittest


spec = importlib.util.spec_from_file_location(
    "stage_clang_tidy_headers",
    Path(__file__).resolve().parents[2] / "contrib/stage_clang_tidy_headers.py",
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class HeaderSearchTests(unittest.TestCase):
    def test_preserves_compiler_order_including_c_headers(self):
        output = """ignored /usr/include/c++/99
#include <...> search starts here:
 /usr/include/c++/11
 /usr/include/c++/11/target
 /usr/include
End of search list.
 /usr/include/c++/99
"""
        self.assertEqual(module.cxx_search_paths(output), [
            Path("/usr/include/c++/11"), Path("/usr/include/c++/11/target"),
            Path("/usr/include"),
        ])

    def test_missing_cpp_headers_is_an_error(self):
        with self.assertRaises(ValueError):
            module.cxx_search_paths("#include <...> search starts here:\n /usr/include\n")


if __name__ == "__main__":
    unittest.main()
