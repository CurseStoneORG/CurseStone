# Coding standards

References: `.clang-format`, `CONTRIBUTING.md` ("Naming and style"), and the surrounding code.

## Language level and compilers
- C++20. In use: GCC 12 (CI), Clang from NDK r26c, MSVC (CI). Avoid GNU-only extensions in portable code.
- The C language stays enabled in CMake only for the vendored Lua (`third_party/lua`, built as C99). Do not add first-party C sources.

## Formatting (from `.clang-format`)
LLVM base, 4-space indent, no tabs, column limit 100, attached braces (`BreakBeforeBraces: Attach`), pointer/reference attached to the type (`int* p`, `const T& r`), access specifiers outdented 2 (`  public:`), `SortIncludes: false`, `IncludeBlocks: Preserve`, short inline functions allowed on one line, no short `if`/loops on one line. Never use Allman braces even if an example elsewhere does. Format only lines you change; keep formatting-only commits separate; no whole-tree reformat.

## Naming (observed)
- Namespaces: `cursestone` and sub-namespaces (`cursestone::block`, `::item`, `::network::protocol`, `::event::player`, `::world`, ...).
- Types `PascalCase`; methods `camelCase` (`getDefaultWorld`, `handleBlockBreak`); data members end with `_` (`dataPath_`, `running_`); constants of the protocol are `UPPER_SNAKE` (`LOGIN_PACKET`, `CURRENT_PROTOCOL`); macros `CURSESTONE_*`.
- Some internal helpers keep legacy snake_case / `mcp_` names (`mcp_binary_read_i16_le`, `mcp_section_new`); keep existing names, do not rename for style.
- Items/blocks: `Item`/`Block` + PascalCase words (`ItemDiamondPickaxe`, `BlockRedstoneOre`); never `ItemDiamondPICKAXE`.
- Files: one primary type per file, file name = type name; headers use `#pragma once` (never `#ifndef` guards).

## Structure and design
- Small focused functions, one responsibility per class, no new dependencies without an issue first.
- Reuse existing abstractions: `utils::Config`, `utils::MainLogger`, `ServerScheduler`, `AsyncPool`, `EventManager`, `PlatformCompat.h`. Do not hand-roll a second logger, scheduler, thread wrapper or config parser.
- Keep headers self-contained and light; forward-declare (as `Server.h` does) instead of including heavy headers.
- Ownership: `std::unique_ptr`/`std::shared_ptr`; avoid raw `new`/`delete`; every allocation has one clear owner.
- Errors: the codebase returns bool/null/error codes and logs; exceptions are essentially absent. Do not introduce exceptions into packet decoding; validate lengths and null before reading wire data and fail the packet, not the process.
- Threads: never mutate world/player state off the main thread; do not call logging, allocation or thread creation from signal handlers.
- Output: use `MainLogger`, not `std::cout`/`printf`, in server code.
- Platform code stays in `cmake/` or guarded blocks; do not add `#ifdef` platform branches in gameplay code.

## Never
Dead code, temporary debug output, unrelated edits, gratuitous public-interface changes, fabricated APIs, assumptions about library behavior (read the header or the vendored source), edits to `third_party/`.
