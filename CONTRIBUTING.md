# Contributing to CenisysPE

## C / C++ split rules

CenisysPE uses a strict **C core + C++ layer** architecture, matching PocketMine 0.15.10 / protocol 84.

### Rule 1 — C files may not include C++ headers
`*.c` files must only `#include` headers that compile cleanly under `gcc -std=c2x`.
No STL, no `new`, no templates, no exceptions.

### Rule 2 — C++ files include C headers via `extern "C"` guards
All C public headers (`math/Vector3.h`, `utils/Utils.h`, `utils/Logger.h`, etc.)
already wrap their declarations in `#ifdef __cplusplus extern "C" { ... } #endif`.
Include them directly from `*.cpp` — no extra wrapping needed.

### Rule 3 — Never inherit from a C struct
Use **composition** instead. The `Position` class is the canonical example:
it holds `double x, y, z` directly and converts to/from `mcp_vector3_t` via
`toV3()` / the explicit constructor.

### Item / Block naming convention

| Pattern | Example | Status |
|---------|---------|--------|
| `Item` + Metal + Noun (PascalCase) | `ItemDiamondPickaxe` | ✅ correct |
| `Item` + Metal + NOUN (ALL-CAPS suffix) | `ItemDiamondPICKAXE` | ❌ wrong |
| `Block` + Adjective + Noun | `BlockRedstoneOre` | ✅ correct |

### C++ wrapper headers
When a C header needs C++ ergonomics (operator overloads, RAII, namespaced
free functions), create a parallel `.hpp` file rather than modifying the `.h`:

| C header | C++ wrapper |
|----------|-------------|
| `math/Vector3.h` | `math/Vector3.hpp` — `cenisyspe::math::Vec3` |
| `utils/Utils.h` | `utils/Utils.hpp` — `cenisyspe::utils::split/trim/toLower` |
| `utils/MainLogger.h` | `utils/MainLogger.hpp` — `cenisyspe::utils::MainLogger` |

### Protocol 84 notes
- Block positions in packets: `int32_t` (signed), not unsigned.
- Entity positions: `float` on the wire, stored as `double` internally.
- Chunk sections: 16×16×16 blocks. Use `mcp_section_new(y)` (not `mcp_chunk_section_new`).
