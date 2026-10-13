#!/usr/bin/env python3
"""Comment linter for CurseStone. Report-only: it never edits or deletes files.

Detects suspicious comment patterns and prints one finding per line:

    path:line: [category] message

Usage:
    python3 tools/lint_comments.py                       # src/ and tools/
    python3 tools/lint_comments.py src/network           # one migration batch
    python3 tools/lint_comments.py src/network src/world
    python3 tools/lint_comments.py --list-categories
    python3 tools/lint_comments.py --strict src          # exit 1 on findings

Categories:
    spanish            likely Spanish prose (needs human translation)
    debug-narrative    debugging-history narrative, stale fix story
    separator          decorative separator / ASCII rule inside comments
    nonascii-punct     suspicious non-ASCII punctuation or invisible characters
    commented-code     commented-out code candidate
    stale-ref          obsolete external source-line reference
    dup-spdx           more than one SPDX-License-Identifier in a file
    missing-file-doc   project header without a file-level Doxygen summary

Design notes / known limitations:
  * Only comment tokens are inspected; string literals and code are skipped,
    so protocol field names and identifiers are never flagged by design.
  * Spanish detection uses accented characters, inverted marks, and a small
    set of Spanish-only words. Technical terms shared with English are not
    used as signals, to avoid false positives on protocol vocabulary.
  * Commented-out code detection is deliberately conservative; snippets that
    read like prose with a trailing semicolon are not flagged, and contiguous
    comment runs containing a "Usage/Example/USO" marker are treated as
    documentation examples and skipped entirely.
  * Findings are advisory: a human reviews each one before any edit.
    Translation and rewriting must be human-reviewed (see
    docs/STYLE_COMMENTS.md).
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from typing import Iterator, List, Tuple

SOURCE_EXTENSIONS = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx")
DEFAULT_PATHS = ["src", "tools"]
THIRD_PARTY = ("third_party", ".git", "__pycache__", "build")

Finding = Tuple[str, int, str, str]  # path, line, category, message

# --- Spanish ----------------------------------------------------------------
SPANISH_ACCENT_RE = re.compile(r"[áéíóúüñÁÉÍÓÚÜÑ¿¡]")
SPANISH_WORDS = (
    "pero", "porque", "también", "así", "cuando", "donde", "aunque",
    "mientras", "después", "mediante", "según", "están", "está", "función",
    "debe", "deben", "sino", "hacia", "muy", "puede", "estos", "estas",
    "devuelve", "comprueba", "sin", "para", "antes",
)
SPANISH_WORD_RE = re.compile(
    r"\b(" + "|".join(sorted(SPANISH_WORDS, key=len, reverse=True)) + r")\b",
    re.IGNORECASE,
)

# --- Debugging narratives ---------------------------------------------------
DEBUG_NARRATIVE_RE = re.compile(
    r"(no (funcionaba|andaba|compilaba|corría)|ya funciona|por fin|"
    r"esto era|esto estaba roto|el bug (era|estaba)|"
    r"\bfinally fixed\b|\bthis fixed (the|it)\b|\bthe bug was\b|"
    r"\bused to (crash|fail|break|hang)\b|\bpreviously (crash|fail|broke|hung)\b|"
    r"\bbisected?\b|\bbisect\b|\bafter (hours|days) (of|debugging)\b|"
    r"\bquick fix for\b|\btemporarily (disable|workaround|skip)\b|"
    r"\bworked around the (bug|crash)\b|\bbroke in (commit|version|build)\b)",
    re.IGNORECASE,
)

# --- Decorative separators --------------------------------------------------
SEP_LINE_RE = re.compile(r"^\s*(//|/\*+|\*+)\s*([=\-~_#*|\\/.]{6,})(\s*\*/)?\s*$")
SEP_INLINE_RE = re.compile(r"^\s*(//|/\*+|\*+)\s*[=\-~#*|]{4,}\s*(\*/)?\s*$")

# --- Suspicious non-ASCII punctuation --------------------------------------
SUSPECT_PUNCT = {
    "\u2018": "left single quotation mark",
    "\u2019": "right single quotation mark",
    "\u201c": "left double quotation mark",
    "\u201d": "right double quotation mark",
    "\u00a0": "non-breaking space",
    "\u200b": "zero-width space",
    "\u200c": "zero-width non-joiner",
    "\u200d": "zero-width joiner",
    "\u2060": "word joiner",
    "\ufeff": "zero-width no-break space (BOM)",
    "\u2028": "line separator",
    "\u2029": "paragraph separator",
}

# --- Commented-out code -----------------------------------------------------
CODE_LINE_RE = re.compile(
    r"^\s*(//|/\*+|\*+)\s*"
    r"(if\s*\(|else\s*\{|for\s*\(|while\s*\(|return\b|switch\s*\(|"
    r"#include\b|#define\b|#ifdef\b|#endif\b|"
    r"(?:static\s+|const\s+|unsigned\s+|struct\s+|class\s+|namespace\s+)*"
    r"(?:void|int|char|bool|size_t|auto|uint\d+_t|int\d+_t|float|double|"
    r"std::\w+|mcp_\w+|cs_\w+)\s+\w+[\s*(=]|"
    r"\w+(?:->|::|\.)\w+\s*\([^;]*\)\s*;|"
    r"\w+\s*=\s*[^=;]+;)"
)
CODE_CONT_RE = re.compile(r"^\s*(//|/\*+|\*+)\s*(\}|\);|\)\s*;|[=;]\s*$)")

# --- Obsolete external source-line references ------------------------------
STALE_REF_RE = re.compile(
    r"((see|ver|compare|comparar|ref\.?)\s+\S+\.(php|java|cpp|h)\b[^\n]{0,40}?"
     r"\bline[s]?\s+\d+"
     r"|\b\S+\.(php|java)\b:\d+"
     r"|\bline\s+\d+\s+of\s+\S+\.(php|java))"
    , re.IGNORECASE,
)

SPDX_RE = re.compile(r"SPDX-License-Identifier\s*:", re.IGNORECASE)
# Any prose comment right after the banner counts as a file-level summary.
FILE_DOC_RE = re.compile(r"^\s*(/\*\*|///|//!|//\s*@|//\s+[^\s])")
NON_DOC_START_RE = re.compile(
    r"^(SPDX-License-Identifier|TODO\b|FIXME\b|XXX\b|HACK\b|NOLINT\b)",
    re.IGNORECASE)
# Example blocks (usage snippets) must not be flagged as commented-out code.
EXAMPLE_MARKER_RE = re.compile(
    r"\b(usage|example|ejemplo|uso)\b\s*[:\u2014-]?", re.IGNORECASE)


def is_third_party(path: str) -> bool:
    parts = path.replace(os.sep, "/").split("/")
    return any(p in THIRD_PARTY for p in parts)


def iter_sources(paths: List[str]) -> Iterator[str]:
    for p in paths:
        if os.path.isfile(p) and p.endswith(SOURCE_EXTENSIONS):
            yield p
        elif os.path.isdir(p):
            for root, dirs, files in os.walk(p):
                dirs[:] = sorted(d for d in dirs if d not in THIRD_PARTY)
                for f in sorted(files):
                    if f.endswith(SOURCE_EXTENSIONS):
                        yield os.path.join(root, f)
        else:
            print(f"warning: path not found: {p}", file=sys.stderr)


def strip_delim(line: str) -> str:
    return re.sub(r"^\s*(//|/\*+|\*+|\*/)\s?", "", line).rstrip()


def comment_lines(src: str) -> Iterator[Tuple[int, str, bool]]:
    """Yield (line_number, comment_text, inside_block_comment).

    ``inside_block_comment`` is True for every physical line of a /* */
    comment (including continuation lines that carry no ``*`` prefix) and
    False for // comments. Callers use it to tell comment continuation lines
    apart from code.

    Conservative lexical scan: skips string/char literals so protocol strings
    and identifiers are never inspected. Raw strings are handled heuristically.
    """
    i, n, line = 0, len(src), 1
    while i < n:
        ch = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if ch in ('"', "'"):
            quote, j = ch, i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == quote:
                    j += 1
                    break
                if src[j] == "\n" and quote == "'":
                    break
                j += 1
            line += src.count("\n", i, j)
            i = j
            continue
        if ch == "R" and nxt == '"':
            m = re.compile(r'R"([^\s()]{0,16})\(').match(src, i)
            if m:
                end = src.find(")" + m.group(1) + '"', m.end())
                j = end + len(m.group(1)) + 3 if end != -1 else n
                line += src.count("\n", i, j)
                i = j
                continue
        if ch == "/" and nxt == "/":
            end = src.find("\n", i)
            end = n if end == -1 else end
            yield (line, src[i:end], False)
            i = end
            continue
        if ch == "/" and nxt == "*":
            start = line
            end = src.find("*/", i + 2)
            end = n if end == -1 else end + 2
            body = src[i:end]
            for k, seg in enumerate(body.split("\n")):
                yield (start + k, seg, True)
            line = start + body.count("\n")
            i = end
            continue
        if ch == "\n":
            line += 1
        i += 1


def lint_file(path: str) -> List[Finding]:
    findings: List[Finding] = []
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            src = fh.read()
    except OSError as exc:
        print(f"warning: cannot read {path}: {exc}", file=sys.stderr)
        return findings

    spdx_lines = [i + 1 for i, l in enumerate(src.splitlines())
                  if SPDX_RE.search(l)]
    if len(spdx_lines) > 1:
        findings.append((
            path, spdx_lines[1], "dup-spdx",
            f"file has {len(spdx_lines)} SPDX-License-Identifier lines "
            f"(lines {', '.join(map(str, spdx_lines))}); keep exactly one"))

    clines = list(comment_lines(src))

    # Mark contiguous // blocks that contain an example/usage marker: their
    # code-looking lines are examples, not commented-out code.
    example_lines = set()
    run_start = None
    for idx, (ln, raw, in_block) in enumerate(clines):
        is_line_comment = not in_block and raw.lstrip().startswith("//")
        if is_line_comment:
            if run_start is None:
                run_start = idx
        else:
            if run_start is not None:
                run = clines[run_start:idx]
                if any(EXAMPLE_MARKER_RE.search(t) for _, t, _ in run):
                    example_lines.update(l for l, _, _ in run)
                run_start = None
    if run_start is not None:
        run = clines[run_start:]
        if any(EXAMPLE_MARKER_RE.search(t) for _, t, _ in run):
            example_lines.update(l for l, _, _ in run)

    is_header = path.endswith((".h", ".hh", ".hpp", ".hxx"))
    if is_header and not is_third_party(path):
        first_doc = None
        seen_code = False
        for ln, text, in_block in clines:
            if in_block:
                # license banner / block comment: never treat its interior
                # lines (which may lack a '*' prefix) as code.
                continue
            if seen_code:
                break
            s = text.strip()
            if not s or s.startswith("*/"):
                continue
            if s.startswith(("//", "/*", "*")):
                if first_doc is None and FILE_DOC_RE.match(text):
                    # Strip comment delimiters before testing the body so an
                    # SPDX line or a TODO line never counts as a file summary.
                    body_s = re.sub(r"^[\s/*]+", "", s)
                    if not SPDX_RE.search(text) \
                            and not NON_DOC_START_RE.search(body_s):
                        first_doc = ln
            else:
                seen_code = True
        if first_doc is None:
            findings.append((
                path, 1, "missing-file-doc",
                "header has no file-level documentation comment after the "
                "license banner (see docs/STYLE_COMMENTS.md section 6)"))

    prev_was_code = False
    for ln, raw, _in_block in clines:
        body = strip_delim(raw)
        if not body.strip():
            prev_was_code = False
            continue

        if SEP_LINE_RE.match(raw) or SEP_INLINE_RE.match(raw):
            findings.append((path, ln, "separator",
                             "decorative separator inside comment"))

        for ch, name in SUSPECT_PUNCT.items():
            if ch in raw:
                findings.append((path, ln, "nonascii-punct",
                                 f"suspicious {name!r} (U+{ord(ch):04X}) "
                                 "in comment"))
                break

        if SPANISH_ACCENT_RE.search(body):
            findings.append((path, ln, "spanish",
                             "likely Spanish comment (accented character); "
                             "translate by hand per STYLE_COMMENTS.md"))
        else:
            words = set(w.lower() for w in SPANISH_WORD_RE.findall(body))
            # two distinct Spanish-only words: single shared words like "para"
            # are too ambiguous on their own
            if len(words) >= 2:
                findings.append((path, ln, "spanish",
                                 f"likely Spanish comment (words: "
                                 f"{', '.join(sorted(words)[:4])})"))

        if DEBUG_NARRATIVE_RE.search(body):
            findings.append((path, ln, "debug-narrative",
                             "debugging-history narrative; restate as the "
                             "current invariant after verifying the code"))

        if STALE_REF_RE.search(body):
            findings.append((path, ln, "stale-ref",
                             "external source-line reference that may be "
                             "obsolete; replace with a stable symbol reference"))

        stripped = body.strip()
        # A code candidate must both look like code and end like a statement.
        # The statement-ending requirement is what keeps prose such as
        # "... mcp_tag_destroy when the refcount drops." out of the report.
        ends_like_statement = bool(re.search(r"[;{}=)]\s*$", stripped))
        looks_code = bool(CODE_LINE_RE.match(raw) or CODE_CONT_RE.match(raw))
        if looks_code and ends_like_statement and len(stripped) > 4 \
                and ln not in example_lines:
            if not re.match(r"^(note|e\.g\.|i\.e\.|for example|see)\b",
                            stripped, re.I):
                if prev_was_code or looks_code:
                    findings.append((path, ln, "commented-code",
                                     "commented-out code candidate; remove or "
                                     "explain the reason it is kept"))
        prev_was_code = looks_code and ends_like_statement

    return findings


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("paths", nargs="*", default=None,
                    help="files or directories (default: src tools)")
    ap.add_argument("--list-categories", action="store_true",
                    help="print category descriptions and exit")
    ap.add_argument("--strict", action="store_true",
                    help="exit with status 1 when findings exist")
    ap.add_argument("--quiet", action="store_true",
                    help="only print the summary line")
    args = ap.parse_args()

    if args.list_categories:
        for line in __doc__.splitlines():
            if line.startswith("    "):
                print(line)
        return 0

    paths = args.paths or DEFAULT_PATHS
    findings: List[Finding] = []
    files = 0
    for path in iter_sources(paths):
        files += 1
        findings.extend(lint_file(path))

    findings.sort(key=lambda f: (f[0], f[1]))
    if not args.quiet:
        for path, ln, cat, msg in findings:
            print(f"{path}:{ln}: [{cat}] {msg}")

    counts: dict = {}
    for _, _, cat, _ in findings:
        counts[cat] = counts.get(cat, 0) + 1
    summary = ", ".join(f"{c}={n}" for c, n in sorted(counts.items()))
    print(f"lint: {len(findings)} findings in {files} files"
          + (f" ({summary})" if summary else ""))
    return 1 if (args.strict and findings) else 0


if __name__ == "__main__":
    sys.exit(main())
