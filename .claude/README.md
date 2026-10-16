# Claude Code configuration for CurseStone

## How it loads
Automatically at session start (Claude Code behavior as documented; not re-verified against an installed copy during the audit, which had no Claude Code installed):
- `.claude/CLAUDE.md`: mandatory summary rules and the file index (Claude Code reads a project `.claude/CLAUDE.md`; there is deliberately no `CLAUDE.md` in the repository root).
- `.claude/rules/*.md`: all nine rule files, so the file names do not matter; being inside `rules/` does.
- `.claude/settings.json`: permissions (read-only git and comment-lint commands allowed; edits to `third_party/` denied). Adjust freely.

Referenced manually (read on demand, not auto-loaded): `docs/STYLE_COMMENTS.md`, `CONTRIBUTING.md`, `BUILDING.md`, `.clang-format`, `Doxyfile`, and this README. If a Claude Code version does not auto-load `rules/`, point `.claude/CLAUDE.md` at the files with `@.claude/rules/<name>.md` imports.

## Unresolved conflicts between the requested rules and existing docs
- Unfinished-work marker: the requested `//TODO: Put what's missing here` differs from the ~50 existing `// TODO:` comments and from `@todo` in `docs/STYLE_COMMENTS.md`. The requested marker is used for new work; reconcile `STYLE_COMMENTS.md` if desired.
- Doxygen scope: the style guide covers public and protected declarations, the requested rule says public. The stricter one is used.
- `docs/STYLE_COMMENTS.md` forbids AI rewriting or translating existing comments. This matches "do not translate unrelated comments" but means legacy Spanish and diary-style comments (baseline: `lint_comments.py src` reported 1120 findings in 1211 files, 792 of them Spanish) need human review.
- The uploaded zip has no `.git`, so Git workflow rules could only be taken from `CONTRIBUTING.md` (which mentions a `.git-blame-ignore-revs` file that is not present).
- Version drift: CMake `1.0.0` vs `CURSESTONE_API "1.0.1"` (see `Architecture.md`).
- Registered blocks: 47 `registerBlock` calls versus about 184 `Block*.h` headers; the rest are possibly unused (**Unverified**).

## Maintenance
Update `Architecture.md` and `Extension-points.md` in the same task that changes a registration point, lifecycle step or config key. Keep `.claude/CLAUDE.md` short: detailed rules belong in `rules/`.
