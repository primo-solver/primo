#!/usr/bin/env python3
"""Check built-book local links and pinned Primo Git links without network access.

Sphinx's linkcheck builder handles other HTTP(S) URLs. This check validates
the generated HTML, including assets, and resolves Primo source links in Git.
It checks source existence and line bounds, not the meaning of the cited code.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit


class Page(HTMLParser):
    def __init__(self, text: str) -> None:
        super().__init__(convert_charrefs=True)
        self.ids: set[str] = set()
        self.links: list[tuple[int, str]] = []
        self.feed(text)

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = dict(attrs)
        for attribute in ("id", "name") if tag == "a" else ("id",):
            if value := values.get(attribute):
                self.ids.add(value)
        attributes = (
            ("href", "src", "poster", "data")
            if tag == "object"
            else ("href", "src", "poster")
        )
        for attribute in attributes:
            if value := values.get(attribute):
                self.links.append((self.getpos()[0], value))


class Checker:
    def __init__(self, repo: Path) -> None:
        self.repo = repo.resolve()
        self.failures: list[dict[str, str | int]] = []
        self.local_links = 0
        self.git_urls: set[str] = set()
        self.external_urls: set[str] = set()
        self.pages = 0
        self._pages: dict[Path, Page] = {}
        self._git_results: dict[str, str | None] = {}

    def page(self, path: Path) -> Page:
        if path not in self._pages:
            self._pages[path] = Page(path.read_text(encoding="utf-8"))
        return self._pages[path]

    def git(self, *args: str) -> bytes:
        return subprocess.check_output(
            ["git", "-C", str(self.repo), *args], stderr=subprocess.PIPE, timeout=10
        )

    def check_git(self, uri: str) -> str | None:
        url = urlsplit(uri)
        parts = unquote(url.path).split("/", 5)
        kind = parts[3]
        if len(parts) < 5 or not re.fullmatch(r"[0-9a-fA-F]{7,40}", parts[4]):
            return "Primo source links must name a commit hash"
        revision = parts[4]
        try:
            self.git("rev-parse", "--verify", f"{revision}^{{commit}}")
            if kind == "commit":
                if len(parts) != 5 or url.fragment:
                    return "unsupported commit URL suffix or fragment"
                return None
            if len(parts) != 6 or not parts[5]:
                return "missing source path"
            obj = f"{revision}:{parts[5]}"
            if self.git("cat-file", "-t", obj).strip() != b"blob":
                return "source URL does not identify a file"
            if url.fragment:
                match = re.fullmatch(r"L(\d+)(?:-L(\d+))?", url.fragment)
                if match is None:
                    return "unsupported Git fragment; use a source line or line range"
                first, last = int(match[1]), int(match[2] or match[1])
                count = len(self.git("show", obj).splitlines())
                if not 1 <= first <= last <= count:
                    return f"line range outside the {count}-line file at {revision}"
        except subprocess.CalledProcessError:
            return f"commit or file unavailable in local Git: {revision}"
        except subprocess.TimeoutExpired:
            return "local Git lookup timed out"
        return None

    def check_link(self, root: Path, page: Path, uri: str) -> str | None:
        url = urlsplit(uri)
        if url.netloc or url.scheme in ("http", "https"):
            if url.hostname == "github.com" and re.match(
                r"^/primo-solver/primo/(blob|commit)(/|$)", url.path
            ):
                self.git_urls.add(uri)
                if uri not in self._git_results:
                    self._git_results[uri] = self.check_git(uri)
                return self._git_results[uri]
            self.external_urls.add(uri)
            return None
        if url.scheme:
            if url.scheme in ("mailto", "tel", "data", "javascript"):
                return None
            return f"unsupported local URL scheme: {url.scheme}"
        self.local_links += 1
        path = unquote(url.path)
        target = root / path.lstrip("/") if path.startswith("/") else page.parent / path
        target = target.resolve() if path else page
        if not target.is_relative_to(root):
            return "local link escapes the built book"
        if target.is_dir():
            target /= "index.html"
        if not target.is_file():
            return "local target does not exist"
        if url.fragment and target.suffix.lower() in (".html", ".htm"):
            fragment = unquote(url.fragment)
            if fragment not in self.page(target).ids:
                return f"missing HTML anchor: {fragment}"
        return None

    def check_book(self, root: Path) -> None:
        root = root.resolve()
        if not (root / "index.html").is_file():
            self.failures.append(
                {
                    "page": str(root),
                    "line": 0,
                    "uri": "",
                    "error": "missing index.html; build the book first",
                }
            )
            return
        for path in sorted(root.rglob("*.html")):
            # Theme templates and downloaded examples are assets, not book pages.
            # A page linking to one still has its target checked below.
            if path.relative_to(root).parts[0] in ("_static", "_downloads", "_sources"):
                continue
            self.pages += 1
            for line, uri in self.page(path).links:
                try:
                    error = self.check_link(root, path, uri)
                except (OSError, ValueError) as exc:
                    error = str(exc)
                if error:
                    self.failures.append(
                        {"page": str(path), "line": line, "uri": uri, "error": error}
                    )

    def report(self) -> dict:
        return {
            "pages": self.pages,
            "local_links": self.local_links,
            "git_urls": len(self.git_urls),
            "external_urls_deferred": len(self.external_urls),
            "failures": self.failures,
        }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("books", type=Path, nargs="+", help="Built HTML directories")
    parser.add_argument(
        "--repo", type=Path, default=Path(__file__).resolve().parents[1]
    )
    parser.add_argument("--report", type=Path, help="Write a JSON report")
    args = parser.parse_args()
    checker = Checker(args.repo)
    for book in args.books:
        checker.check_book(book)
    report = checker.report()
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for failure in checker.failures:
        print(
            f"{failure['page']}:{failure['line']}: {failure['error']} ({failure['uri']})"
        )
    print(
        f"{checker.pages} pages; {checker.local_links} local links; "
        f"{len(checker.git_urls)} unique Git links; {len(checker.failures)} failures"
    )
    print(f"{len(checker.external_urls)} external URLs deferred to Sphinx linkcheck")
    return int(bool(checker.failures))


if __name__ == "__main__":
    raise SystemExit(main())
