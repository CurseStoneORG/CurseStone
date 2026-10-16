# Architecture (verified during the repository audit)

Everything here was read from sources, build files or CI. Items marked **Unverified** were not confirmed; do not treat them as fact. "Grep-based" means found by text search, not by reading every call path.

## 1. Identity and constants
- MCPE 0.15.x server, protocol 84 only (`CURRENT_PROTOCOL = 84` in `network/protocol/Info.h`; `PROTOCOL_VERSION 84` in `src/CurseStone.h`). LGPL-3.0-or-later.
- `src/CurseStone.h` (umbrella header): `CURSESTONE_API "1.0.2"`, codename "Obsidian", tick rate 20/s (`CURSESTONE_TICK_MS 50`), `CURSESTONE_LEVELDB` default 0.
- Version drift: `CMakeLists.txt` says `VERSION 1.0.2` while `CurseStone.h` says `1.0.2`. Do not "fix" one silently; report it.
- Model: docs say modeled on a 0.15.10 legacy server; `CurseStone.h` says "Inspired by a 4.0.0 legacy server". Both appear; the protocol-84 target is the authoritative one.

## 2. Build shape (`CMakeLists.txt`)
- One executable, `CurseStone`. `src/**/*.c` and `src/**/*.cpp` are globbed with `CONFIGURE_DEPENDS`: new files need no CMake edit. Include root is `src/` (`#include "world/World.h"`).
- Language levels: C23 (`CMAKE_C_STANDARD 23`), C++20, both `REQUIRED`. Warnings: `-Wall -Wextra` (GNU/Clang), `/W3` (MSVC). Default build type: Release.
- Lua 5.4 is vendored (`third_party/lua`) and built as static lib `lua` (`lua.c`/`luac.c` excluded, warnings off, `LUA_USE_POSIX` off-Windows).
- Links: Threads, zlib (required), LevelDB (optional via `cmake/FindLevelDB.cmake`; adds `CURSESTONE_LEVELDB=1`), `dl` on UNIX, `log`+`android` on Android, `ws2_32 wsock32` on Windows. `ENABLE_EXPORTS ON` for symbolized backtraces.
- Size under `src/`: 74 `.c`, 395 `.cpp`, 739 `.h`, 3 `.hpp` (audit snapshot).

## 3. Language boundary
- C core: `math/`, `nbt/` (+`nbt/tag/`), `logger/`, `network/raklib/`, C files of `utils/` (Binary, BinaryStream, Logger, MainLogger, Color, Random, Terminal, TextFormat, ThreadPort, UUID, Utils), `world/format/Chunk.c`, `ChunkSection.c`, `anvil/AnvilChunkCodec.c`.
- C++ layer: everything else. C headers carry `extern "C"` guards; C++ includes them directly.
- Observed conventions: all 739 headers use `#pragma once` (no `#ifndef` guards); exceptions are nearly unused (1 `throw`, about 30 `try` blocks, so errors are return values plus logging); ownership is by `std::unique_ptr`/`std::shared_ptr` (about 436 `make_*` calls vs 8 raw `new`); logging goes through `utils::MainLogger::get().info/warn/error` (C code uses `mcp_log*` / `mcp_main_logger_*`).

## 4. Lifecycle
- `src/CurseStone.cpp`: `main`, SIGINT/SIGTERM handler that only sets a `volatile sig_atomic_t` flag (no logging, malloc or thread creation in handlers), crash-dump hookup, first-run wizard check (`wizard/Installer`; "needs setup" means `cursestone.yml` is missing). Platform backtrace: `execinfo.h` (glibc/macOS) or `_Unwind_Backtrace` (Android).
- `Server` (`src/Server.h` and `src/Server.cpp`) is a singleton via `Server::getInstance()`.
- Boot, in order (`Server::start`): data dir -> `initLogger` (`<data>/server.log`) -> `utils::writeDefaultConfigs` (creates missing files) -> `loadConfig` -> `loadOps` -> `registerBlocksAndItems` -> `SimpleCommandMap` + `registerDefaultCommands` -> `CraftingManager::registerBuiltinRecipes` (recipes are code, not a file) -> `AsyncPool(async_workers)` -> `openNetwork` (`Network`, `RakLibInterface::start`) -> `loadDefaultWorld` -> `PluginManager::loadAll(<data>/plugins, <data>/data_pl)` -> `startCommandReader` -> 200 ms flush pause.
- Tick (`Server::tick`, absolute 50 ms schedule): reap players pending removal -> `ServerScheduler::tick` -> world ticks -> block entities (furnaces) -> global GC (`MemoryManager`) -> network pass 1 -> player updates -> batched packet flush -> network pass 2 -> incremental autosave. `WorkbenchManager` times each phase.
- Shutdown: `Server::shutdown()` flips `running_` and starts a detached watchdog that calls `_Exit(1)` if the loop has not exited within 5 s. Cleanup runs after the loop in `runTickLoop`: `PluginManager::disableAll()` -> `saveAll()` -> console reader stop -> `AsyncPool::stop()` -> "Shutdown complete.". Saving is deliberately post-loop because `/stop` comes from the `CommandReader` thread.

## 5. Subsystems
- **Network** (`network/`): `Network` owns `RakLibInterface` (UDP, own thread), `QueryHandler`, RCON (`rcon/`). `network/raklib` is the C RakNet implementation (Session, SessionManager, UDPServerSocket). `network/protocol` has about 129 files of packets. Incoming: `Network.cpp` -> `PacketPool::create(pid)` (a `switch` over IDs from `Info.h`) -> `Player::handleDataPacket` (`player/PlayerNetwork.cpp`). `CompressBatchedTask` compresses batches via `AsyncPool`.
- **Player** (`Player.h/.cpp` + `player/`): one class split by concern: `PlayerLogin`, `PlayerNetwork`, `PlayerTick` (movement, chat, actions, interact), `PlayerBlocks`, `PlayerCombat`, `PlayerContainers`, `PlayerInventory`, `PlayerChunks`, `PlayerWorld`, `PlayerSleep`, `PlayerStats`, `PlayerIdentity`. `player/PlayerInternal.h` is internal to these files. Position stores the **feet**; wire move packets carry the **eye** (eye height 1.62).
- **World** (`world/`): `World`, `ChunkManager`, `LightEngine`, `Explosion`, `PortalRegistry`. Formats via `WorldProvider`/`WorldProviderManager`: Anvil (`.mca`, default, has an async writer thread) and LevelDB (stub unless `CURSESTONE_LEVELDB=1`). Generators chosen in `World.cpp` by name: `normal` (default), `flat`, `nether`, `void`/`empty`; Normal uses `biome/`, `carver/`, `noise/`, `object/`, `populator/`, `structure/`, and chunk generation runs as `GenerationTask` on the async pool. Extra subdirs: `particle/`, `sound/`.
- **Blocks/Items/Tiles/Entities**: blocks are flyweight singletons derived from `Block` (virtual `id()` etc.), registered in `block/BlockRegistry.cpp`; items from `Item`, registered in `item/ItemRegistry.cpp` (`Item::registerAll`); block entities (`Tile`: chest, furnace, sign, spawner) built through `tile/TileFactory.cpp::createTileForBlock`; entities: `Entity` -> `Living` -> `Creature` -> `Animal` / `Monster`, `Human`, `Projectile`, plus concrete mobs (Chicken, Cow, Creeper, Pig, Sheep, Skeleton, Wolf, Zombie) and non-living entities (item drop, XP orb, falling sand, primed TNT, arrow). No pathfinding or mob AI per README.
- **Inventory** (`inventory/`): Base/Player/Chest/Furnace inventories, `CraftingManager`, `Recipe`, `CreativeItems.h`.
- **Events** (`event/`): `EventManager` (typed handler lists, priorities, cancellation) and about 120 event classes by domain (block, entity, inventory, player, plugin, potion, redstone, server, weather, world). Grep-based: `EventManager` call sites live in `player/*.cpp` and `entity/Living.cpp`; how many of the ~120 classes are actually fired was **not** determined.
- **Commands** (`command/`): `SimpleCommandMap`, `CommandReader` (console thread), built-ins in `command/defaults/` (deop, gamemode, give, help, kick, kill, list, op, plugins, save, say, stop, tp, time, version, weather, workbench), registered in `command/Defaults.cpp`.
- **Permissions** (`permission/`): `Permissible`, `PermissibleBase`, `PermissionAttachment`, ban lists, `DefaultPermissions::registerCorePermissions`. Ops are loaded/saved by `Server` (`loadOps`/`saveOps`).
- **Scheduler** (`scheduler/`): `ServerScheduler` (sync tasks plus async completion callbacks, always ticked), `AsyncPool` (workers; exceptions are caught per task), `FileWriteTask`.
- **Plugins** (`plugin/`): see section 6.
- **Support**: `utils/` (`Config` minimal YAML+INI, `DefaultConfig`, `PlatformCompat.h`, `ThreadPort`, `VersionString`, `BlockIterator`), `diagnostics/`, `workbench/` (`/workbench` profiler with watchdog thread), `CrashDump`, `MemoryManager` (RSS budget + GC interval), `ThreadManager` (joins registered threads), `wizard/` (first-run installer, `InstallerLang`), `physics/`, `redstone/`.

## 6. Lua plugin host
- Scan: only folders under `<data>/plugins/` that contain `plugin.yml` are plugins; a lone `main.lua` is ignored with a warning. Manifest fields read in `LuaPlugin.cpp`: `name`, `version`, `author`, `api`, `main`. Entry callbacks: `onEnable`, `onDisable`. Plugins load and enable alphabetically; duplicate names/folders leave all duplicates disabled with a warning.
- Per-plugin data: `<data>/data_pl/<pluginFolder>/` at the server root, reachable only through the `data.*` API (`PathSafe.h` guards relative paths). `resourcesDir_` is `<plugin>/resources` (plugin-owned, unrelated to the repo-root `resources/`).
- Lua globals registered in `Bindings*.cpp`: `log`, `server` (including `server.worlds`), `player`, `plugin`, `events`, `commands`, `data`, `import`. Event names exposed to Lua are strings such as `PlayerJoin`, `PlayerQuit`, `PlayerChat`, `BlockBreak`, `BlockPlace`, `EntityDamage`, `EntityDamageByEntity`, `PlayerDeath`, `PlayerRespawn`, bed events (list continues in `BindingsEvents.cpp`).
- Execution limits: every Lua call goes through `LuaPlugin::pcall`. A VM count hook (`LuaPlugin::checkLimits`) enforces `settings.plugin-instruction-limit` and `settings.plugin-time-limit-ms` per outermost call and keeps raising until that call ends, so a Lua-side `pcall` cannot swallow it. `settings.plugin-max-errors` consecutive failed outermost calls refuse further calls and disable the plugin on the next tick. Limits are read once in `Server::loadConfig`; C functions such as `string.rep` are not interruptible by the hook.
- Any change to a Lua binding is a public API change; the external docs live in `CurseStoneTeam/cursestone-docs`.

## 7. Configuration
- Read from the working directory by `Server::loadConfig()`; defaults live in `utils/DefaultConfig.cpp` and are written when files are missing.
- `server.properties` (INI): `server-ip`, `server-port`, `motd`, `max-players`, `gamemode`, `difficulty`, `white-list`, `online-mode`, `view-distance`, `world-name`, `world-seed`.
- `cursestone.yml` (YAML): sections `settings` (`async-workers`, `autosave-ticks`, `advanced-log`, `plugin-instruction-limit`, `plugin-time-limit-ms`, `plugin-max-errors`), `world` (`format`, `generator`), `memory` (`max-mb`, `gc-interval-ticks`); network/logging keys in the repo-root sample are **not** read by `loadConfig` (grep-based).
- Language strings: `wizard/InstallerLang` holds built-in installer text.

## 8. Threads
RakLib thread; console `CommandReader`; `AsyncPool` workers; Anvil writer; workbench watchdog; shutdown watchdog. Game state is mutated on the main tick thread; hand off work with `ServerScheduler`/`AsyncPool`. `Server::playersSnapshot()` exists so other threads iterate a copy of the roster. In C code use `utils/ThreadPort.h`; do not include pthread headers where MSVC compiles.

## 9. Unresolved questions (do not assume)
- Repo-root `resources/` (`cursestone.yml`, `server.properties`, `eng.ini`): no reference found in `CMakeLists.txt` or `src/`; its YAML layout differs from `DefaultConfig.cpp` (for example `server.autosave-ticks`). Purpose **Unverified**; it looks like a stale sample.
- Only 47 `registerBlock` calls exist in `BlockRegistry.cpp` while `src/block` holds about 184 `Block*.h` headers; whether the rest are unused, registered elsewhere, or intentionally absent was not determined. A block header existing does not mean the block is registered.
- LevelDB provider is TODO-stubbed; behavior with `CURSESTONE_LEVELDB=1` **Unverified**.
- `Server.cpp` contains stubbed members (whitelist add/remove/reload, `getConfig*`, parts of player-list data, `broadcastPacket`); not audited one by one.
- Blocks, items, entities, generators and individual packets were sampled, not exhaustively read.
