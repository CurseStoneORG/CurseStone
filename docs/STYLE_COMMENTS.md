# CurseStone Comment and Documentation Style Guide

This guide governs comments and Doxygen documentation in CurseStone.
It is enforced by review plus the automated tooling in `tools/`:

- `tools/comment_inventory.py` — read-only inventory of every comment (CSV).
- `tools/lint_comments.py` — reports suspicious comment patterns; it never edits files.

The rules below apply to **comment text only**. Comment cleanup must never change
executable code, algorithms, public APIs, network protocols, or runtime behavior.
Comment edits and formatting-only edits are kept in separate commits.

## 1. Language

- Technical comments are written in **American English**.
- Comments that are already in English are edited in place, not translated.
- Comments still in Spanish are translated **by a human reviewer** who verifies
  the current implementation first. Automated translation or AI rewriting of
  comments is not allowed.
- Do not invent behavior. Every statement in a comment must be supported by the
  code it describes, by the protocol specification, or by a documented invariant.

## 2. What deserves a comment

Document with Doxygen:

- Every public and protected class, struct, union, and enumeration declaration.
- Every public and protected member function, including free functions exposed
  through headers.
- Public macros and preprocessor constants with non-obvious meaning.
- Public data members whose purpose is not obvious from the name.
- File-level headers (see section 6).

Use ordinary (`//` or `/* */`) comments only when they explain **non-obvious**
behavior: a workaround, a compatibility rule, a protocol quirk, a threading or
ownership requirement, a performance rationale, or an invariant the reader
cannot infer from the code.

Do **not** add comments that merely restate the code or the identifier name.

## 3. Doxygen conventions

- Use the `@` form: `@brief`, `@param`, `@return`, `@note`, `@warning`, `@todo`.
- `@brief` may be omitted when the first sentence is a single-line summary
  (`JAVADOC_AUTOBRIEF` is enabled in the `Doxyfile`), but using it explicitly
  is encouraged for complex APIs.
- Document every parameter of a documented function (`@param name ...`) and
  the return value (`@return ...`) for non-void functions.
- Use `@note` for supplementary constraints (platform, configuration) and
  `@warning` for caller-visible hazards (ownership transfer, lifetime,
  thread-safety, preconditions that crash when violated).
- Use `@todo` for known gaps; never leave a stale `@todo` for work that is done.
- For trivial overrides that add no behavior, use `@copydoc Base::method()`
  or `/** @copydoc ... */`-style inherited documentation instead of restating
  the parent description.
- Never document a parameter that does not exist or describe semantics the code
  does not implement.

Example:

```cpp
/**
 * @brief Reads a signed little-endian 16-bit field from the wire.
 *
 * Protocol 84 encodes most integer fields little-endian; only NBT uses
 * big-endian in Java-compatible contexts.
 *
 * @param p  pointer to at least 2 readable bytes; must not be null.
 * @return the decoded value.
 * @warning The caller must have validated the length; this performs no checks.
 */
int16_t mcp_binary_read_i16_le(const void* p);
```

## 4. What to remove or rewrite

- **Debugging diaries**: narratives about past crashes, bisects, or "this used
  to fail" stories that no longer describe the current code. Convert them into
  a statement of the current invariant or constraint, keeping the technical
  reason (e.g. why an unloaded-chunk guard exists) intact.
- **Decorative separators**: runs of `=`, `-`, `~`, or ASCII-art rules inside
  comments. One blank comment line is enough.
- **Redundant restatements** of the code.
- **Obsolete references** to external source lines (e.g. "see Server.php
  line 1234") when the referenced file has drifted; replace with the stable
  upstream symbol or document the behavior directly.
- **Duplicate SPDX identifiers** (a file must carry exactly one
  `SPDX-License-Identifier` line).
- Commented-out code that serves no documentation purpose — but only after
  confirming it is not kept as a reference for a protocol or platform quirk.

Preserve (never delete during cleanup):

- Invariants, thread-safety and locking requirements, ownership/lifetime rules.
- Compatibility, migration, and platform notes.
- Protocol details, field layouts, magic values, and wire-format constraints.
- Attribution and third-party references.

## 5. Prohibited or restricted changes

- No AI-generated or automatically translated comment rewrites.
- Comments are not deleted merely because they look machine-generated; they
  are judged by content.
- No license banner or copyright header changes without the legal/attribution
  review described in `CONTRIBUTING.md`.
- No changes to `#line`-sensitive constructs or code layout that could affect
  `__LINE__`-based diagnostics (macros, logging, assertions).

## 6. File headers

Every project-owned `.h` / `.c` / `.cpp` file keeps exactly one header block:

1. The existing license banner block (do not alter its text without review).
2. Exactly one `// SPDX-License-Identifier: LGPL-3.0-or-later` line.
3. A one- or two-sentence American English summary of the file's role
   (this is the Doxygen file comment; `tools/lint_comments.py` flags headers
   that lack it).

`third_party/` files are exempt: their headers, licenses, and comments are
never rewritten.

## 7. Tooling workflow (per migration batch)

```bash
# 1. Inventory (read-only)
python3 tools/comment_inventory.py src/network --csv build/comment_inventory.csv

# 2. Review the CSV, edit comments by hand in small batches

# 3. Lint the batch
python3 tools/lint_comments.py src/network

# 4. Regenerate docs and check warnings
doxygen Doxyfile

# 5. Build and run the tests for the touched area, review the diff,
#    then commit — only after validation and authorization.
```

Batch order: core server/utilities → networking/protocols → world/terrain →
players/inventory → blocks/items/entities → events/commands/plugins →
tools/build infrastructure → README and contributor docs.
