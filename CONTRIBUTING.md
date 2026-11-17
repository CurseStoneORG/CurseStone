# Contributing to CurseStone

Thank you for your interest in CurseStone. This document explains how the project is organized, which conventions every change follows, and how to get your work merged.

**Contents**

1. [Ways to contribute](#1-ways-to-contribute)
2. [Setting up](#2-setting-up)
3. [Architecture at a glance](#3-architecture-at-a-glance)
4. [Naming and style](#4-naming-and-style)
5. [Protocol 84 notes](#5-protocol-84-notes)
6. [Where things go](#6-where-things-go)
7. [Testing your change](#7-testing-your-change)
8. [Commits and pull requests](#8-commits-and-pull-requests)
9. [Reporting bugs](#9-reporting-bugs)
10. [Plugins](#10-plugins)
11. [License](#11-license)

---

## 1. Ways to contribute

- **Report a bug** with a clear way to reproduce it.
- **Fix a bug** or **implement a missing feature** of MCPE 0.15.x (protocol 84).
- **Improve performance or stability**, ideally with numbers from `/workbench`.
- **Improve documentation**, including this file and the README.
- **Add checks** to `tools/` that protect behavior you care about.

For anything larger than a small fix, open an issue first so the approach can be agreed before you invest time.

---

## 2. Setting up

1. Fork the repository and clone your fork.
2. Install the dependencies listed in the README (CMake 3.20+, a C++20 compiler — GCC 12+, Clang or MSVC —, zlib, LevelDB). Lua 5.4 is bundled in `third_party/lua`.
3. Build in debug mode:

   ```bash
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build build --parallel
   ./build/CurseStone
   ```

   Presets `linux-debug`, `linux-release` and `android-arm64` are available in `CMakePresets.json`. If you do not have CMake, `./build_local.sh` compiles with the same sources and flags. The full build guide is in [docs/BUILDING.md](docs/BUILDING.md).
4. Create a branch for your work:

   ```bash
   git checkout -b fix/short-description
   ```

---

## 3. Architecture at a glance

CurseStone is a **single C++20 codebase** targeting MCPE 0.15.x (protocol 84). There are no first-party sources in any other language; the C language stays enabled in the build only for the vendored Lua 5.4 in `third_party/`.

Conventions that keep the tree consistent:

- All server code lives in the `cursestone` namespace and its sub-namespaces.
- Ownership is expressed with `std::unique_ptr` / `std::shared_ptr`; errors are return values plus logging through `utils::MainLogger`, not exceptions.
- Headers are self-contained and guarded with `#pragma once`. Do not pull heavy headers into other headers; forward-declare instead.
- Game state is mutated on the main tick thread only. Off-thread work goes through `ServerScheduler` / `AsyncPool`, and threads hand data over instead of sharing mutable state.
- Code must build on Linux, macOS, Windows and Android. Keep platform-specific code in `cmake/` or inside clearly guarded blocks; do not scatter `#ifdef` branches through gameplay code.
- A new block, item, command, packet or event does nothing until it is registered — see section 6.

---

## 4. Naming and style

### Items and blocks

| Pattern | Example | |
|---|---|---|
| `Item` + Material + Noun, PascalCase | `ItemDiamondPickaxe` | ✅ |
| `Item` + Material + NOUN in capitals | `ItemDiamondPICKAXE` | ❌ |
| `Block` + Adjective + Noun | `BlockRedstoneOre` | ✅ |

### General

- Types are `PascalCase`, methods are `camelCase`, data members end with `_` (`dataPath_`, `running_`), protocol constants are `UPPER_SNAKE`.
- Match the style of the file you are editing. Do not reformat code you are not changing.
- Do not add a new dependency without discussing it in an issue first.
- Do not leave debugging output, commented-out code or unrelated changes in your pull request.

### Comments and documentation

- Comments and Doxygen documentation follow [docs/STYLE_COMMENTS.md](docs/STYLE_COMMENTS.md): American English, concise and technically accurate, Doxygen for public/protected declarations, no debugging diaries or decorative separators, and no invented behavior.
- Public and protected classes, functions, enumerations and macros get Doxygen comments (`@brief`, `@param`, `@return`, `@note`, `@warning`); trivial overrides reuse inherited documentation.
- Preserve invariants, protocol details, threading/ownership requirements, and compatibility notes when editing comments.
- Two read-only helpers support review:

  ```bash
  python3 tools/comment_inventory.py src/<dir> --csv build/inventory.csv
  python3 tools/lint_comments.py src/<dir>
  ```

- API documentation is generated with `doxygen Doxyfile` (output in `build/docs/html/`, git-ignored). Missing-doc warnings are non-fatal.

---

## 5. Protocol 84 notes

- Block positions in packets are `int32_t` (**signed**), not unsigned.
- Entity positions are `float` on the wire and `double` internally.
- Chunk sections are 16×16×16 blocks.
- `Position` stores the **feet**; movement packets on the wire carry the **eye** position (eye height 1.62).

Keep wire formats unchanged unless your change is explicitly a protocol change, and call it out in the pull request.

---

## 6. Where things go

| You are adding | Put it in |
|---|---|
| A block | `src/block/` |
| An item or enchantment | `src/item/` |
| A block entity (chest, furnace, sign...) | `src/tile/` |
| A built-in command | `src/command/defaults/`, then register it in `src/command/Defaults.cpp` |
| A packet | `src/network/protocol/` |
| Terrain, biome, noise or populator logic | `src/world/generator/` |
| A world storage format | `src/world/format/` |
| An event | `src/event/` |
| A Lua or native plugin API surface | `src/plugin/` |
| A standalone check or benchmark | `tools/` |

---

## 7. Testing your change

`tools/` contains standalone check programs (`*_check.cpp`), Python end-to-end checks that talk to a running server, fuzzing helpers and benchmarks. If a check exists for the area you changed, run it. If none exists, consider adding one.

Before you open a pull request, make sure that:

- [ ] the project builds from a clean directory with no new warnings,
- [ ] the server starts, accepts a client and shuts down cleanly with `/stop`,
- [ ] the behavior you changed works in game,
- [ ] existing relevant checks in `tools/` still pass.

For performance problems or disconnects, `/workbench start` followed by `/workbench report` gives you TPS, tick timing, packet logs and RakNet counters to attach to your pull request.

---

## 8. Commits and pull requests

**Commits**

- One logical change per commit.
- Write the subject in the imperative mood, up to about 72 characters: `Fix chunk unload crash on leveldb worlds`.
- Use the body to explain *why*, not just *what*.

**Pull requests**

1. Keep each pull request focused on a single topic.
2. Describe what changed, why, and how you tested it.
3. Call out anything that changes the wire protocol, the world format or the config files.
4. Link the related issue, if there is one.
5. Respond to review comments by pushing new commits; the maintainers may ask you to squash at the end.

---

## 9. Reporting bugs

Open an issue and include:

- the CurseStone version (run `/version`) and your platform,
- exact steps to reproduce the problem,
- what you expected and what happened instead,
- the relevant log output,
- a `/workbench report` for performance issues or disconnects,
- the crash dump, if the server crashed.

If you find a security problem, do not post it publicly: contact the maintainers privately instead.

---

## 10. Plugins

CurseStone supports two plugin load types: **Lua plugins** (a folder with `plugin.yml`) and **native C++ plugins** (a bare `.so`/`.dll` file, no manifest). The full API reference and build instructions live in [docs/PLUGINS.md](docs/PLUGINS.md); working examples are [`plugins/SpawnProtection/`](plugins/SpawnProtection/README.md) (Lua) and [`plugins/HelloNative/`](plugins/HelloNative/README.md) (C++).

---

## 11. License

CurseStone is licensed under **LGPL-3.0-or-later**. By submitting a contribution you agree that it is released under the same license.
