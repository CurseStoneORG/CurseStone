# Coding standards

References: `.clang-format`, `CONTRIBUTING.md` section 4, and the surrounding code.

## Language levels and compilers
- C core: C23 (`-std=c2x`). The Windows CI job overrides to **C17**, so `*.c`/`*.h` must also compile as C17 on MSVC: avoid C23-only features (`constexpr`, `nullptr`, `#embed`, `typeof`, attributes needing C23).
- C++20. In use: GCC 12 (CI), Clang from NDK r26c, MSVC (CI). Avoid GNU-only extensions in portable code.

## Formatting (from `.clang-format`)
LLVM base, 4-space indent, no tabs, column limit 100, attached braces (`BreakBeforeBraces: Attach`), pointer/reference attached to the type (`int* p`, `const T& r`), access specifiers outdented 2 (`  public:`), `SortIncludes: false`, `IncludeBlocks: Preserve`, short inline functions allowed on one line, no short `if`/loops on one line. Never use Allman braces even if an example elsewhere does. `clang-format` was unavailable in the audit sandbox: format only lines you change; keep formatting-only commits separate; no whole-tree reformat.

## Naming (observed)
- Namespaces: `cursestone` and sub-namespaces (`cursestone::block`, `::item`, `::network::protocol`, `::event::player`, `::world`, ...).
- Types `PascalCase`; methods `camelCase` (`getDefaultWorld`, `handleBlockBreak`); data members end with `_` (`dataPath_`, `running_`); constants of the protocol are `UPPER_SNAKE` (`LOGIN_PACKET`, `CURRENT_PROTOCOL`); macros `CURSESTONE_*`.
- Some legacy functions are snake_case (`register_all_blocks`); keep existing names, do not rename for style.
- Items/blocks: `Item`/`Block` + PascalCase words (`ItemDiamondPickaxe`, `BlockRedstoneOre`); never `ItemDiamondPICKAXE`.
- C code: `mcp_` prefix, snake_case, `_t` typedef suffix (`mcp_bstream_t`, `mcp_vector3_t`); paired `_new`/`_free`/`_destroy`.
- Files: one primary type per file, file name = type name; headers use `#pragma once` (all 739 do; never `#ifndef` guards).

## Structure and design
- Small focused functions, one responsibility per class, no new dependencies without an issue first.
- Public C headers: `extern "C"` guard, C++-safe. Add C++ conveniences in a parallel `.hpp`; never edit the `.h` for that.
- Reuse existing abstractions: `utils::Config`, `utils::MainLogger`, `ServerScheduler`, `AsyncPool`, `EventManager`, `PlatformCompat.h`, `ThreadPort.h`. Do not hand-roll a second logger, scheduler, thread wrapper or config parser.
- Keep headers self-contained and light; forward-declare (as `Server.h` does) instead of including heavy headers.
- Ownership: `std::unique_ptr`/`std::shared_ptr`; avoid raw `new`/`delete` in C++; in C, pair every `*_new` with its destroy/free and document who frees.
- Errors: the codebase returns bool/null/error codes and logs; exceptions are essentially absent (1 `throw`). Do not introduce exceptions across the C boundary or into packet decoding; validate lengths and null before reading wire data and fail the packet, not the process.
- Threads: never mutate world/player state off the main thread; do not call logging, allocation or thread creation from signal handlers.
- Output: use `MainLogger`, not `std::cout`/`printf`, in server code (the 13 existing direct-print sites are legacy).
- Platform code stays in `cmake/` or guarded blocks; do not add `#ifdef` platform branches in gameplay code.

## Never
Dead code, temporary debug output, unrelated edits, gratuitous public-interface changes, fabricated APIs, assumptions about library behavior (read the header or the vendored source), edits to `third_party/`.
