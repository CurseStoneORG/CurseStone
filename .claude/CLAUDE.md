# CurseStone — Claude Code project instructions

CurseStone is an open source server for Minecraft: Pocket Edition 0.15.x (protocol 84): a plain C core (C23) plus C++20 game logic, with embedded Lua 5.4 plugins. License: LGPL-3.0-or-later.

Inspect the code before changing it. Base every claim on files you actually read. Never invent APIs, build commands, platforms or behavior.

## Mandatory rules (always apply)

1. Comments: newly written comments and Doxygen are **English only** (American English, per `docs/STYLE_COMMENTS.md`), short, and never diagnostic. Do not translate or rewrite unrelated existing comments.
2. Never leave debugging notes, investigation logs, test results or "I fixed X" narration in source. Put completed-work notes in `changes.md`.
3. Unfinished work uses the exact marker `//TODO: Put what's missing here`, followed by a concise English description when known. Every TODO is also listed in `changes.md`.
4. Respect the C/C++ split: `*.c` never depends on C++, C code must also build as C17 (MSVC), never inherit from a C struct. See `.claude/rules/Architecture.md`.
   A new block, item, command or packet does nothing until registered: see `.claude/rules/Extension-points.md`.
5. Keep diffs focused. No unrelated refactors, no mass reformatting, no new dependencies without discussion.
6. Never claim a build or test passed unless the command actually ran and succeeded. Report what could not be run.
7. Do not commit or push unless the user asks.
8. Code and docs disagree sometimes (see `.claude/README.md`): trust the code, report the discrepancy, do not silently rewrite unrelated docs.

## Rule files

Files in `.claude/rules/` are loaded automatically at session start by Claude Code (all `.md` files there, recursively; nine files). They are listed here so the set is explicit:

- `.claude/rules/Instructions.md` — how to investigate, change, validate and report work
- `.claude/rules/Coding-standards.md` — formatting, naming, code quality
- `.claude/rules/Rules.md` — enforceable comment and hygiene rules (A–E)
- `.claude/rules/Architecture.md` — verified architecture, lifecycle, config, Lua host, unresolved questions
- `.claude/rules/Extension-points.md` — exact steps and registration points for adding blocks, items, packets, commands, events, Lua API, config keys
- `.claude/rules/Testing.md` — real test and validation mechanisms
- `.claude/rules/Compatibility.md` — platforms, toolchains, protocol
- `.claude/rules/Documentation.md` — documentation policy
- `.claude/rules/Changes.md` — how to maintain the root `changes.md`

## Read on demand (not auto-loaded)

Open these with the Read tool when the task touches them:

- `docs/STYLE_COMMENTS.md` — authoritative comment and Doxygen guide
- `CONTRIBUTING.md` — C/C++ boundary rules, naming, "where things go", PR workflow
- `BUILDING.md`, `CMakePresets.json`, `build_local.sh` — build procedures
- `.clang-format`, `Doxyfile` — formatting and documentation config
- `.claude/README.md` — how this configuration is organized and what is unverified

## Final review before declaring a task done

Run the checklist in `.claude/rules/Instructions.md` ("Final review"), then update `changes.md`.
