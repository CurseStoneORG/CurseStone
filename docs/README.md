# CurseStone Documentation

Index of the documentation that lives in this repository. Plugin API and generated
reference docs are published separately (see [External documentation](#external-documentation)).

## Getting started

| Document | Description |
|---|---|
| [BUILDING.md](BUILDING.md) | How to build CurseStone: GitHub Actions, Linux x86_64, Android/Termux and ARM64 cross-compile (in Spanish). |
| [../CONTRIBUTING.md](../CONTRIBUTING.md) | Project organization, the C / C++ split, naming and style, protocol 84 notes, where things go and the PR workflow. |
| [STYLE_COMMENTS.md](STYLE_COMMENTS.md) | Comment and Doxygen style guide, enforced by review and `tools/lint_comments.py`. |

## Audits and investigations

| Document | Description |
|---|---|
| [CURSESTONE_ARCHITECTURE_AUDIT.md](audits/CURSESTONE_ARCHITECTURE_AUDIT.md) | Full native C++ architecture audit: performance, memory, scalability and maintainability. |
| [CURSESTONE_RAKLIB_UAF_INVESTIGATION.md](audits/CURSESTONE_RAKLIB_UAF_INVESTIGATION.md) | Investigation of the RakLib session-index use-after-free after a kick. |
| [memory-audit.md](audits/memory-audit.md) | Deep memory audit report with the fixes applied. |
| [ownership-map.md](audits/ownership-map.md) | Ownership and lifecycle map with diagrams. |
| [test-results.md](audits/test-results.md) | Builds, runs and commands behind the memory audit, with results. |
| [c-to-cpp-assessment.md](audits/c-to-cpp-assessment.md) | Executive assessment of migrating the remaining C core to C++. |

## Release notes

Per-version changelogs, in English and Spanish, are in [`../changelogs/`](../changelogs/README.md).

## Components

| Component | Description |
|---|---|
| [`plugins/SpawnProtection`](../plugins/SpawnProtection/README.md) | Example Lua plugin that protects the spawn area. |
| [`test/bot`](../test/bot/README.md) | Protocol-84 test client used to exercise a running server. |

## External documentation

- Official website: https://cursestoneorg.github.io/
- Plugin and installation docs: https://cursestoneorg.github.io/docs/
- Doxygen API reference: https://cursestoneorg.github.io/doxygen-docs/
