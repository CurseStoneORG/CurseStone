# AtomikPE

**AtomikPE** is a cross-platform Minecraft Bedrock Edition server
software written in C/C++. It mirrors the module structure of
[PocketMine-MP 4.0.0](https://github.com/pmmp/PocketMine-MP) (PHP),
the same way Nukkit mirrors it for the Java ecosystem.

- **Target protocol:** MCPE/Bedrock protocol **84** (Minecraft **0.15.10**)
- **Languages:** C20 (`-std=c2x`) for RakLib / NBT / Binary / math / chunk
  data structs, C++20 (`-std=c++20`) for game logic (Server, Player, Level,
  Block, Item, Entity, Event, Command, Scheduler, Inventory)
- **No plugin system** (yet)
- **No mob AI** (entities exist as data structs only, no pathfinding)
- **Cross-platform:** Linux, macOS, Windows (MSVC + MinGW), Android NDK r26+

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

The resulting executable is `AtomikPE`. It reads its configuration from
`resources/atomikpe.yml` (relative to the working directory).

## Running

```bash
./AtomikPE --data ./data
```

On first launch the installer wizard (Phase 22) will offer to generate
a default `atomikpe.yml` and a fresh world. After setup the server
binds UDP 19132 by default and accepts RakNet/MCPE connections.

## Project layout

```
AtomikPE/
├── CMakeLists.txt
├── README.md
├── .gitignore
├── docs/                          # Protocol 84 + mcacpp format specs
├── cmake/                         # Platform toolchain files
├── resources/                     # Server-side assets
│   ├── creativeitems.json         # Copied from PocketMine-MP 4.0.0
│   ├── recipes.json               # Copied from PocketMine-MP 4.0.0
│   ├── atomikpe.yml            # Main config
│   ├── eng.ini                    # English locale
│   └── biome_definitions.json     # Biome data
└── src/
    ├── main.h                     # Global header / forward decls
    ├── main.cpp                   # Entry point
    ├── Server.{h,cpp}             # Core singleton + tick loop
    ├── Player.{h,cpp}             # Player session
    ├── IPlayer.h
    ├── OfflinePlayer.{h,cpp}
    ├── MemoryManager.{h,cpp}
    ├── CrashDump.{h,cpp}
    ├── ThreadManager.{h,cpp}
    ├── block/                     # 40+ block classes
    ├── item/                      # 80+ item classes
    │   └── enchantment/
    ├── entity/                    # 25+ entity classes (no AI)
    ├── inventory/                 # Inventory + crafting
    ├── event/                     # Event system
    ├── command/                   # Built-in commands
    │   └── defaults/
    ├── level/                     # World + chunk formats
    │   ├── format/
    │   │   ├── mcacpp/            # Default AtomikPE format
    │   │   ├── leveldb/            # MCPE native format
    │   │   ├── mca/                # Java Anvil .mca
    │   │   └── mcapm/             # MCAPM variant
    │   ├── generator/
    │   │   ├── biome/
    │   │   ├── noise/
    │   │   ├── object/
    │   │   └── populator/
    │   ├── particle/
    │   └── sound/
    ├── network/                   # Network stack
    │   ├── raklib/                # Pure-C RakNet
    │   ├── protocol/              # 65 MCPE protocol-84 packets
    │   ├── query/                 # GS4 query handler
    │   └── rcon/                  # RCON listener
    ├── nbt/                       # NBT reader/writer
    │   └── tag/
    ├── math/                      # Vector + AABB
    ├── permission/                # Permissions + ban list
    ├── scheduler/                 # Tick scheduler + async pool
    ├── tile/                      # Block entities (chest/furnace/sign)
    ├── utils/                     # Logger, BinaryStream, Config, etc.
    └── wizard/                    # First-run setup
```

## World format

The default world format is **mcacpp** — a region-file format inspired by
Minecraft Java's `.mca` but tailored for MCPE chunks. The format is
specified in `docs/MCACPP_FORMAT.md`. Other selectable formats (via
`level.format` in `atomikpe.yml`): `leveldb`, `mca`, `mcapm`.

## Implementation status

All 22 build phases complete:

| Phase | Module                          | Status       |
|-------|---------------------------------|--------------|
| 01    | skeleton (main, CMake, README)  | ✅ done      |
| 02    | utils (Binary, Logger, Config)  | ✅ done      |
| 03    | math (Vector, AABB)             | ✅ done      |
| 04    | nbt (all tag types + reader)    | ✅ done      |
| 05    | raklib (UDP + Session + ACK)    | ✅ done      |
| 06    | protocol (all 65 packets)        | ✅ done      |
| 07    | network (Network, RakLibIface)  | ✅ done      |
| 08    | level/format (mcacpp, leveldb)  | ✅ done      |
| 09    | level/generator (noise, biomes)| ✅ done      |
| 10    | level (Level, ChunkManager)     | ✅ done      |
| 11    | block (40+ blocks)              | ✅ done      |
| 12    | item (80+ items + enchantment)  | ✅ done      |
| 13    | entity (Player, mobs, proj)     | ✅ done      |
| 14    | tile (chest, furnace, sign)     | ✅ done      |
| 15    | inventory (45 slots, recipes)   | ✅ done      |
| 16    | event (player/block/entity)     | ✅ done      |
| 17    | command (13 defaults)           | ✅ done      |
| 18    | permission (perm, bans)         | ✅ done      |
| 19    | scheduler (AsyncPool, Tasks)    | ✅ done      |
| 20    | Player (session lifecycle)      | ✅ done      |
| 21    | Server (tick loop, full boot)   | ✅ done      |
| 22    | wizard (first-run installer)    | ✅ done      |

## License

LGPL-3.0-or-later, mirroring PocketMine-MP.
