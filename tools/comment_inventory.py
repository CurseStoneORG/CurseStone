#!/usr/bin/env python3
"""Read-only comment inventory for CurseStone.

Scans C/C++ sources and produces a reviewable CSV of every comment, with
start/end line numbers, the original text (unchanged), and a category when
classification is reliable. It NEVER modifies source files.

Usage:
    python3 tools/comment_inventory.py                       # src/ and tools/
    python3 tools/comment_inventory.py src/network           # one directory
    python3 tools/comment_inventory.py src/network src/world # several paths
    python3 tools/comment_inventory.py --csv build/inventory.csv src

CSV columns:
    file, start_line, end_line, category, text

Categories (only assigned when reliable; empty means unclassified):
    license     - SPDX / copyright / license notice text
    banner      - the CurseStone ASCII-art license banner
    doxygen     - Doxygen-style documentation (/** ... */ or @-commands)
    file-doc    - file-level summary comment right after the banner
    separator   - purely decorative rule/separator lines
    todo        - contains TODO/FIXME/XXX/HACK
    spanish     - likely Spanish (accented letters or multiple Spanish-only words)
    code        - likely commented-out code (conservative heuristic)
    other       - any other comment with no confident classification

Known limitations (documented on purpose):
  * This is a lexical scanner, not a full C/C++ parser. String and character
    literals are skipped, but raw string literals (R"...") and line
    continuations are only handled heuristically; a "//" or "/*" inside an
    unhandled raw string may be mistaken for a comment start.
  * Code inside #if 0 / disabled preprocessor branches is not special-cased.
  * Comments produced by macro expansion are invisible to the scanner.
  * End-of-line comments are reported with the line of the comment start only
    (start_line == end_line for single-line comments).
  * Categories are heuristics for triage; a human must review before any edit.
    Empty category simply means "no confident classification".
  * License banners spanning many lines are reported as one record, which may
    put ASCII art and legal text in the same row (category: banner wins).
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import sys
from typing import Iterator, List, Tuple

SOURCE_EXTENSIONS = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx")
DEFAULT_PATHS = ["src", "tools"]

SPDX_RE = re.compile(r"SPDX-License-Identifier|Copyright\s*\(C\)|Licensed under", re.I)
BANNER_MARKERS = ("_____", "/  __ \\", "CurseStone | Open Source Server Software")
DOXYGEN_RE = re.compile(r"^\s*(/\*\*|//!|///<)|[@\\](brief|param|return|note|warning|todo|copydoc|see)\b")
TODO_RE = re.compile(r"\b(TODO|FIXME|XXX|HACK)\b")
SPANISH_ACCENT_RE = re.compile(r"[áéíóúüñÁÉÍÓÚÜÑ¿¡]")
SPANISH_WORD_RE = re.compile(
    r"\b(pero|porque|tambi[eé]n|as[ií]|cuando|donde|debe[nr]?|aunque|mientras|"
    r"despu[eé]s|funci[oó]n|mediante|aunque|seg[uú]n|est[aá]n?|sino)\b",
    re.I,
)
SEPARATOR_RE = re.compile(r"^[\s=*/~\-#\\_|\-.]+$")
CODE_HINT_RE = re.compile(
    r"(;\s*$|^\s*(if|for|while|switch|return|else|int|char|void|size_t|auto|const\b)"
    r"[\s({]|->\w+\(|=\s*[^=;]+;|\w+\([^)]*\)\s*;|\b(std::|mcp_|cs_)\w+)"
)


def classify(text: str, is_file_doc: bool) -> str:
    """Assign a category only when the signal is reliable; else '' or 'other'."""
    stripped = text.strip()
    if not stripped:
        return ""
    body = re.sub(r"^/\*+|\*+/$|^//+", "", stripped).strip()
    if not body:
        return ""
    if any(m in text for m in BANNER_MARKERS) and "CurseStone" in text:
        return "banner"
    if SPDX_RE.search(text):
        return "license"
    if DOXYGEN_RE.search(stripped) or stripped.startswith("/**"):
        return "doxygen"
    if is_file_doc:
        return "file-doc"
    # Decorative-only lines (allow a couple of words? no: pure decoration only).
    compact = re.sub(r"\s", "", body)
    if compact and SEPARATOR_RE.match(compact) and len(compact) >= 4:
        return "separator"
    if TODO_RE.search(text):
        return "todo"
    if SPANISH_ACCENT_RE.search(body):
        return "spanish"
    if len(set(SPANISH_WORD_RE.findall(body))) >= 2:
        return "spanish"
    lines = [ln for ln in body.splitlines() if ln.strip()]
    codeish = sum(1 for ln in lines if CODE_HINT_RE.search(ln))
    if lines and codeish >= max(1, len(lines) // 2) and codeish >= 1:
        if any(ch in body for ch in ";={}()") and not body.endswith("."):
            return "code"
    return "other"


def scan_file(path: str) -> Iterator[Tuple[str, int, int, str, str]]:
    """Yield (file, start_line, end_line, category, text) for each comment."""
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            src = fh.read()
    except OSError as exc:
        print(f"warning: cannot read {path}: {exc}", file=sys.stderr)
        return

    i = 0
    n = len(src)
    line = 1
    first_comment_seen = False
    banner_done = False

    def advance(to: int) -> int:
        nonlocal line
        line += src.count("\n", i, to)
        return to

    while i < n:
        ch = src[i]
        nxt = src[i + 1] if i + 1 < n else ""

        # Skip string / char literals (conservative handling of escapes).
        if ch == '"' or ch == "'":
            quote = ch
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == quote:
                    j += 1
                    break
                if src[j] == "\n" and quote == "'":
                    break  # unterminated char literal; bail to avoid runaway
                j += 1
            i = advance(j)
            continue

        # Raw string literals R"delim( ... )delim"
        if ch == "R" and nxt == '"':
            m = re.compile(r'R"([^\s()]{0,16})\(').match(src, i)
            if m:
                delim = m.group(1)
                end = src.find(")" + delim + '"', m.end())
                i = advance(end + len(delim) + 3 if end != -1 else n)
                continue

        # Line comment.
        if ch == "/" and nxt == "/":
            start_line = line
            end = src.find("\n", i)
            end = n if end == -1 else end
            text = src[i:end]
            is_doc = (not first_comment_seen) and not banner_done
            cat = classify(text, is_doc and not SPDX_RE.search(text))
            yield (path, start_line, start_line, cat, text)
            if not first_comment_seen:
                first_comment_seen = True
            if "SPDX" in text or "Copyright" in text:
                banner_done = True
            i = advance(end)
            continue

        # Block comment.
        if ch == "/" and nxt == "*":
            start_line = line
            end = src.find("*/", i + 2)
            end = n if end == -1 else end + 2
            text = src[i:end]
            end_line = line + text.count("\n")
            is_doc = (not first_comment_seen) and not banner_done
            cat = classify(text, is_doc and not SPDX_RE.search(text))
            yield (path, start_line, end_line, cat, text)
            first_comment_seen = True
            if "SPDX" in text or "Copyright" in text:
                banner_done = True
            line = end_line
            i = end
            continue

        if ch == "\n":
            line += 1
        i += 1


def iter_sources(paths: List[str]) -> Iterator[str]:
    for p in paths:
        if os.path.isfile(p):
            if p.endswith(SOURCE_EXTENSIONS):
                yield p
        elif os.path.isdir(p):
            for root, dirs, files in os.walk(p):
                dirs[:] = sorted(
                    d for d in dirs if d not in ("__pycache__", "build", ".git")
                )
                for f in sorted(files):
                    if f.endswith(SOURCE_EXTENSIONS):
                        yield os.path.join(root, f)
        else:
            print(f"warning: path not found: {p}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("paths", nargs="*", default=None,
                    help="files or directories to scan (default: src tools)")
    ap.add_argument("--csv", metavar="FILE", default="-",
                    help="output CSV file ('-' = stdout)")
    ap.add_argument("--summary", action="store_true",
                    help="print a per-category summary to stderr")
    args = ap.parse_args()

    paths = args.paths or DEFAULT_PATHS
    out = sys.stdout if args.csv == "-" else open(args.csv, "w", newline="",
                                                  encoding="utf-8")
    counts: dict = {}
    files = 0
    rows = 0
    try:
        writer = csv.writer(out, quoting=csv.QUOTE_ALL, lineterminator="\n")
        writer.writerow(["file", "start_line", "end_line", "category", "text"])
        for path in iter_sources(paths):
            files += 1
            for rec in scan_file(path):
                writer.writerow(rec)
                rows += 1
                counts[rec[3] or "(unclassified)"] = counts.get(
                    rec[3] or "(unclassified)", 0) + 1
    finally:
        if out is not sys.stdout:
            out.close()

    print(f"inventory: {rows} comments in {files} files", file=sys.stderr)
    if args.summary:
        for cat, num in sorted(counts.items(), key=lambda kv: -kv[1]):
            print(f"  {cat:15s} {num}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
