# Contributing to CurseStone

Thank you for your interest in CurseStone. This document explains how the project is organized, what rules every change must follow, and how to get your work merged.

**Contents**

1. [Ways to contribute](#1-ways-to-contribute)
2. [Setting up](#2-setting-up)
3. [Architecture rules: the C / C++ split](#3-architecture-rules-the-c--c-split)
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
2. Install the dependencies listed in the README (CMake 3.20+, GCC 12+ or Clang with C23 and C++20 support, zlib, LevelDB). Lua 5.4 is bundled in `third_party/lua`.
3. Build in debug mode:

   ```bash
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
   cmake --build build --parallel
   ./build/CurseStone
   ```

   Presets `linux-debug`, `linux-release` and `android-arm64` are available in `CMakePresets.json`. If you do not have CMake, `./build_local.sh` compiles with the same sources and flags. The full build guide is in [BUILDING.md](BUILDING.md).
4. Create a branch for your work:

   ```bash
   git checkout -b fix/short-description
   ```

---

## 3. Architecture rules: the C / C++ split

CurseStone is a strict **C core plus a C++ layer**, modeled on a legacy 0.15.10 server / protocol 84.

| Layer | Language | Contains |
|---|---|---|
| Core | C23 (`-std=c2x`) | RakNet, NBT, binary I/O, math, chunk data |
| Game | C++20 | Server, Player, World, Block, Item, Entity, Event, Command, Scheduler, Inventory |

The two layers talk through plain C structs and `extern "C"` boundaries. These three rules keep that boundary clean, and a pull request that breaks them will not be merged.

### Rule 1: C files never depend on C++

A `*.c` file may only include headers that compile cleanly under `gcc -std=c2x`. That means no STL, no `new`, no templates, no exceptions, no `class`.

### Rule 2: C headers are C++-safe, so C++ includes them directly

Every public C header wraps its declarations like this:

```c
#ifdef __cplusplus
extern "C" {
#endif

/* declarations */

#ifdef __cplusplus
}
#endif
```

Because of that, `*.cpp` files include C headers (`math/Vector3.h`, `utils/Utils.h`, `utils/Logger.h`, ...) with a plain `#include`. Do not add extra `extern "C"` wrapping at the include site.

### Rule 3: never inherit from a C struct

Use **composition**. The canonical example is `Position`: it stores `double x, y, z` itself and converts to and from the C type `mcp_vector3_t` through `toV3()` and an explicit constructor.

```cpp
// Good: composition, explicit conversion
Position p(1.0, 64.0, 1.0);
mcp_vector3_t v = p.toV3();

// Bad: struct Position : mcp_vector3_t { ... };
```

### C++ wrappers for C headers

When a C header needs C++ conveniences (operators, RAII, namespaced helpers), add a parallel `.hpp` file. Do not change the `.h`.

| C header | C++ wrapper |
|---|---|
| `math/Vector3.h` | `math/Vector3.hpp`: `cursestone::math::Vec3` |
| `utils/Utils.h` | `utils/Utils.hpp`: `cursestone::utils::split/trim/toLower` |
| `utils/MainLogger.h` | `utils/MainLogger.hpp`: `cursestone::utils::MainLogger` |

---

## 4. Naming and style

### Items and blocks

| Pattern | Example | |
|---|---|---|
| `Item` + Material + Noun, PascalCase | `ItemDiamondPickaxe` | ✅ |
| `Item` + Material + NOUN in capitals | `ItemDiamondPICKAXE` | ❌ |
| `Block` + Adjective + Noun | `BlockRedstoneOre` | ✅ |

### General

- C++ code lives in the `cursestone` namespace.
- Match the style of the file you are editing. Do not reformat code you are not changing.
- Keep headers self-contained, and do not pull heavy headers into other headers.
- Do not add a new dependency without discussing it in an issue first.
- Code must build on Linux, macOS, Windows and Android. Keep platform-specific code in `cmake/` or inside clearly guarded blocks.
- Do not leave debugging output, commented-out code or unrelated changes in your pull request.

### Comments and documentation

- Comments and Doxygen documentation follow [docs/STYLE_COMMENTS.md](docs/STYLE_COMMENTS.md): American English, concise and technically accurate, Doxygen for public/protected declarations, no debugging diaries or decorative separators, and no invented behavior.
- Public and protected classes, functions, enumerations, macros, and API members get Doxygen comments (`@brief`, `@param`, `@return`, `@note`, `@warning`); trivial overrides reuse inherited documentation.
- Preserve invariants, protocol details, threading/ownership requirements, and compatibility notes when editing comments.
- Two read-only helpers support review:

  ```bash
  python3 tools/comment_inventory.py src/<dir> --csv build/inventory.csv
  python3 tools/lint_comments.py src/<dir>
  ```

- API documentation is generated with `doxygen Doxyfile` (output in `build/docs/html/`, git-ignored). Missing-doc warnings are non-fatal while the migration is in progress.
- When touching formatting, keep formatting-only commits separate from comment/documentation commits.

---

## 5. Protocol 84 notes

- Block positions in packets are `int32_t` (**signed**), not unsigned.
- Entity positions are `float` on the wire and `double` internally.
- Chunk sections are 16×16×16 blocks. Create them with `mcp_section_new(y)`, not `mcp_chunk_section_new`.

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
| A standalone check or benchmark | `tools/` |

---

## 7. Testing your change

`tools/` contains standalone check programs (`*_check.cpp`), smoke tests, benchmarks and a few shell runners such as `tools/run_nbt_test.sh`. If a check exists for the area you changed, run it. If none exists, consider adding one.

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

Documentation for the Lua plugin system lives in its own repository: <https://github.com/CurseStoneTeam/cursestone-docs>.

---

## 11. License

CurseStone is licensed under **LGPL-3.0-or-later**. By submitting a contribution you agree that it is released under the same license.

