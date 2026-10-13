# CurseStone

<p align="left">
  <img alt="License" src="https://img.shields.io/badge/license-LGPL--3.0--or--later-blue">
  <img alt="C++" src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white">
  <img alt="C" src="https://img.shields.io/badge/C-C2X-A8B9CC?logo=c&logoColor=black">
  <img alt="Protocol" src="https://img.shields.io/badge/MCPE%20protocol-84-8A2BE2">
  <img alt="Lua" src="https://img.shields.io/badge/Lua-5.4%20embedded-2C2D72?logo=lua&logoColor=white">
  <img alt="CMake" src="https://img.shields.io/badge/CMake-%E2%89%A53.20-064F8C?logo=cmake&logoColor=white">
</p>

Server software for **Minecraft: Pocket Edition 0.15.x** (protocol 84), written in C and C++.

| | |
|---|---|
| **Version** | 1.0.1 (codename *Obsidian*) |
| **Target** | MCPE 0.15.x · protocol 84 |
| **Languages** | C23 core · C++20 game layer |
| **Platforms** | Linux, macOS, Windows (MSVC and MinGW), Android (NDK r26+ / Termux) |
| **License** | LGPL-3.0-or-later |

## Table of contents

- [Features](#features)
- [Quick start](#quick-start)
- [Building](#building)
- [Configuration](#configuration)
- [Console and in-game commands](#console-and-in-game-commands)
- [World storage](#world-storage)
- [Plugins](#plugins)
- [Diagnostics](#diagnostics)
- [Project layout](#project-layout)
- [Contributing](#contributing)
- [License](#license)

## Features

- Strict **C core + C++ layer** architecture: RakNet, NBT, binary I/O, math and chunk data are plain C; game logic is C++20. See [CONTRIBUTING.md](CONTRIBUTING.md) for the exact rules.
- Two world formats: `anvil` (default) and `leveldb`.
- Built-in terrain generation with biomes, noise and populators.
- Lua 5.4 plugin system, with the engine compiled into the binary.
- Query (GS4) and RCON support.
- Ban list and permission attachments.
- Async worker pool and a global memory manager.
- First-run setup wizard.
- Built-in runtime profiler (`workbench`) for performance and disconnect debugging.
- Crash dumps for post-mortem analysis.

## Quick start

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/CurseStone
```

If no configuration exists in the working directory, the first-run wizard generates one and creates a fresh world. The server listens on **UDP 19132** by default; open that port in your firewall.

## Building

### Requirements

| Dependency | Notes |
|---|---|
| CMake 3.20+ | Ninja is recommended |
| GCC 12+ or Clang | Must support C23 (`-std=c2x`) and C++20 |
| zlib | Use `ZLIB_ROOT` if it is not detected |
| LevelDB | Use `LEVELDB_INCLUDE_DIRS` / `LEVELDB_LIBRARIES` if it is not detected (snappy may be required on Linux) |
| Lua 5.4 | Bundled in `third_party/lua`, nothing to install |

### Linux (Debian/Ubuntu)

```bash
sudo apt-get install cmake ninja-build gcc-12 g++-12 zlib1g-dev libleveldb-dev libsnappy-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Android (Termux)

```bash
pkg update && pkg install -y cmake ninja clang zlib leveldb
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Presets

`CMakePresets.json` ships three presets: `linux-debug`, `linux-release` and `android-arm64`.

```bash
cmake --preset linux-release
cmake --build --preset linux-release
```

### Prebuilt binaries

GitHub Actions builds **Linux x86_64** and **Android ARM64** binaries on every push; download them from the workflow's *Artifacts*. A release workflow can also publish a release with a custom name, description and attached binaries.

The full build guide (cross-compiling with the NDK, release workflow, CMake variables) is in [BUILDING.md](BUILDING.md).

## Configuration

Two files are read from the working directory:

| File | Purpose |
|---|---|
| `server.properties` | Simple key/value file. Checked first if present. |
| `cursestone.yml` | Native format. Also exposes memory limits and the async worker count. |

Both are seeded from `resources/` on first run.

Common `server.properties` keys:

| Key | Default | Description |
|---|---|---|
| `server-port` | `19132` | UDP port |
| `max-players` | `20` | Player limit |
| `view-distance` | `8` | Chunk view distance |
| `gamemode` | `0` | 0 survival · 1 creative · 2 adventure |
| `difficulty` | `1` | 0 peaceful · 1 easy · 2 normal · 3 hard |
| `world-name` / `world-seed` | `world` / empty | Level folder name and seed |
| `world-format` | `anvil` | `anvil` or `leveldb` |
| `autosave-ticks` | `6000` | Auto-save interval (6000 ticks = 5 min at 20 TPS) |
| `compression-level` | `7` | zlib level, 1 to 9 |

Extra `cursestone.yml` sections: `memory` (`max-mb`, `gc-interval-ticks`), `settings.async-workers` and `logging` (`level`, `colored`).

## Console and in-game commands

| Command | Description |
|---|---|
| `/help`, `/?` | List the commands you can use |
| `/version` | Software version and target protocol |
| `/list` | Online players |
| `/say` | Broadcast a message |
| `/kick` | Kick a player |
| `/op`, `/deop` | Grant or revoke operator status |
| `/gamemode`, `/gm` | Change game mode |
| `/give` | Give an item |
| `/teleport`, `/tp` | Teleport |
| `/time`, `/weather` | Control time and weather |
| `/save` | Save the world |
| `/stop` | Stop the server |
| `/plugins` | List loaded plugins |
| `/workbench` | Runtime diagnostics (see below) |

## World storage

Worlds can be stored in either format, selected with `world-format` (`server.properties`) or `world.format` (`cursestone.yml`):

- **`anvil`** (default): region files based on Minecraft Java's `.mca`, adapted for MCPE chunks.
- **`leveldb`**: Bedrock's native format (beta).

## Plugins

CurseStone can be extended with Lua plugins. All plugin documentation, the API reference and a ready-to-run example live in a separate repository:

**https://github.com/CurseStoneTeam/cursestone-docs**

## Diagnostics

`workbench/` is a runtime profiler built into the server, controlled with `/workbench start|stop|report|status`. It tracks:

- live TPS and per-phase tick timing,
- inbound and outbound packet logs,
- RakNet send and retransmit counters,
- a watchdog thread that detects main-thread stalls,
- a state dump on player disconnects.

It is meant for debugging performance problems and disconnects without attaching an external profiler.

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
├── world/
│   ├── format/           world storage: anvil, leveldb
│   └── generator/        terrain, biomes, noise, populators
├── network/
│   ├── raklib/           RakNet implementation (C)
│   ├── protocol/         MCPE protocol 84 packets
│   ├── query/            GS4 query
│   └── rcon/             RCON
├── plugin/               Lua plugin runtime
├── nbt/                  NBT reader/writer
├── math/                 vector and AABB
├── logger/               async logging
├── workbench/            runtime diagnostics
├── diagnostics/          crash and state diagnostics
├── physics/, redstone/   physics and redstone logic
├── utils/                config, binary streams, misc helpers
└── wizard/               first-run setup
third_party/lua/          bundled Lua 5.4
resources/                default config files and language file
tools/                    standalone checks, smoke tests and benchmarks
cmake/                    platform and dependency modules
```

## Contributing

Contributions are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) first: it covers the C/C++ boundary rules, naming conventions, protocol notes and the pull request workflow.

## License

LGPL-3.0-or-later. See <https://www.gnu.org/licenses/lgpl-3.0.html>.
