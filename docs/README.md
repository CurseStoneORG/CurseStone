# CurseStone Documentation

Index of the documentation that lives in this repository. Generated
reference docs are published separately (see [External documentation](#external-documentation)).

## Getting started

| Document | Description |
|---|---|
| [BUILDING.md](BUILDING.md) | How to build CurseStone: GitHub Actions, Linux x86_64, Windows, Android/Termux and ARM64 cross-compile (in Spanish). |
| [PLUGINS.md](PLUGINS.md) | Plugin API reference: the two load types (Lua folders, native C++ .so/.dll files), lifecycle, API tables and build instructions. |
| [../CONTRIBUTING.md](../CONTRIBUTING.md) | Project conventions, naming and style, protocol 84 notes, where things go and the PR workflow. |
| [STYLE_COMMENTS.md](STYLE_COMMENTS.md) | Comment and Doxygen style guide, enforced by review and `tools/lint_comments.py`. |

## Architecture audit

| Document | Description |
|---|---|
| [CURSESTONE_ARCHITECTURE_AUDIT.md](audits/CURSESTONE_ARCHITECTURE_AUDIT.md) | Native C++ architecture audit (performance, memory, scalability, maintainability) with a numbered findings table. Historical snapshot: it reflects the tree at its stated commit. |

## Engineering notes

| Document | Description |
|---|---|
| [THREADING.md](THREADING.md) | Threading investigation: CurseStone's current thread map vs. Aquamarine's thread moves (what we already have, what to adopt, what to avoid). |

## Release notes

Per-version changelogs, in English and Spanish, are in [`../changelogs/`](../changelogs/README.md).

## Components

| Component | Description |
|---|---|
| [`plugins/SpawnProtection`](../plugins/SpawnProtection/README.md) | Example Lua plugin that protects the spawn area. |
| [`plugins/HelloNative`](../plugins/HelloNative/README.md) | Reference native C++ plugin (.so/.dll) with build instructions. |
| [`test/bot`](../test/bot/README.md) | Protocol-84 test client used to exercise a running server. |

## External documentation

- Official website: https://cursestoneorg.github.io/
- Plugin and installation docs: https://cursestoneorg.github.io/docs/
- Doxygen API reference: https://cursestoneorg.github.io/doxygen-docs/
