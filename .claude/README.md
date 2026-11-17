# Claude Code configuration for CurseStone

## How it loads
Automatically at session start (Claude Code behavior):
- `.claude/CLAUDE.md`: mandatory summary rules and the file index (Claude Code reads a project `.claude/CLAUDE.md`; there is deliberately no `CLAUDE.md` in the repository root).
- `.claude/rules/*.md`: all eight rule files; the file names do not matter, being inside `rules/` does.
- `.claude/settings.json`: permissions (read-only git and comment-lint commands allowed; edits to `third_party/` denied). Adjust freely.

Referenced manually (read on demand, not auto-loaded): `docs/STYLE_COMMENTS.md`, `CONTRIBUTING.md`, `docs/BUILDING.md`, `docs/PLUGINS.md`, `.clang-format`, `Doxyfile`, and this README. If a Claude Code version does not auto-load `rules/`, point `.claude/CLAUDE.md` at the files with `@.claude/rules/<name>.md` imports.

## Known convention conflicts
- Unfinished-work marker: the requested `//TODO: Put what's missing here` differs from the existing `// TODO:` comments in the tree and from `@todo` in `docs/STYLE_COMMENTS.md`. The requested marker is used for new work; leave existing ones alone.
- Doxygen scope: the style guide covers public and protected declarations, the requested rule says public. The stricter one is used.

## Maintenance
Update `Architecture.md` and `Extension-points.md` in the same task that changes a registration point, lifecycle step or config key. Keep `.claude/CLAUDE.md` short: detailed rules belong in `rules/`.
