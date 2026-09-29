# CurseStone

Server software for Minecraft: Pocket Edition 0.15.10 (Protocol 84), written in C and C++.

The codebase follows a strict split: low-level structures (RakNet, NBT, binary I/O, math, chunk data) are plain C, compiled as `-std=c2x`. Game logic (Server, Player, Level, Block, Item, Entity, Event, Command, Scheduler, Inventory) is C++20. The two layers communicate through plain C structs and `extern "C"` boundaries — no inheritance across the boundary, only composition. See `CONTRIBUTING.md` for the exact rules.

Supported platforms: Linux, macOS, Windows (MSVC and MinGW), and Android via NDK r26+.

## Building

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The resulting binary is `CurseStone` for ready to start server

## Running

```
./CurseStone
```

If no configuration exists in the working directory, the first-run wizard generates one and creates a fresh world. The server binds UDP `19132` by default.

## Configuration

Two config files are read from the data directory:

- `server.properties` — key/value file, checked first if present.
- `cursestone.yml` — the native format

Both are seeded from `resources/` on first run. `server.properties` covers the common settings (port, max players, view distance, gamemode, difficulty, world name/seed); `cursestone.yml` additionally exposes memory limits and async worker count.

## Project layout

```
src/
├── Server.{h,cpp}        server singleton and tick loop
├── Player.{h,cpp}        player session and packet handling
├── OfflinePlayer, IPlayer, MemoryManager, CrashDump, ThreadManager
├── block/                block types
├── item/                 item types, enchantments
├── entity/               entity data (no pathfinding/AI)
├── tile/                 block entities: chest, furnace, sign, ...
├── inventory/            inventories and crafting
├── event/                event bus
├── command/defaults/     built-in commands
├── permission/           permission attachments and ban list
├── scheduler/            tick scheduler and async worker pool
├── level/
│   ├── format/           world storage: anvil, leveldb
│   └── generator/        terrain, biomes, noise, populators
├── network/
│   ├── raklib/           RakNet implementation (C)
│   ├── protocol/         MCPE protocol 84 packets
│   ├── query/            GS4 query
│   └── rcon/             RCON
├── nbt/                  NBT reader/writer
├── math/                 vector and AABB
├── logger/               async logging
├── workbench/            runtime diagnostics (see below)
├── utils/                config, binary streams, misc helpers
└── wizard/               first-run setup
```

## World storage

Worlds are stored in either format, selected via `level-format` / `level.format`:

- `anvil` — default. Region-file format based on Minecraft Java's `.mca`, adapted for MCPE chunks.
- `leveldb` — Bedrock's native format ( beta )

## Diagnostics

`workbench/` is a runtime profiler built into the server, controlled with `/workbench start|stop|report|status`. It tracks live TPS, per-phase tick timing, in/out packet logs, RakNet send/retransmit counters, a watchdog thread for detecting main-thread stalls, and a state dump on player disconnects. Intended for debugging performance issues and disconnects without attaching an external profiler.

## Contributing

Coding conventions (C/C++ boundary rules, naming, protocol notes) are in `CONTRIBUTING.md`.

## License

LGPL-3.0-or-later.
