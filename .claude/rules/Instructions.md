# General development instructions

## Purpose
CurseStone is a lightweight server for Minecraft: Pocket Edition 0.15.x (protocol 84), modeled on a legacy 0.15.10 server. It is intentionally not a vanilla server (for example, entities have no pathfinding or mob AI; see `README.md`).

## Before modifying code
1. Read the file and its header, then its callers and callees. Use `grep -rn` for symbols; do not rely on file names.
2. For anything touching a subsystem, read the matching section of `Architecture.md` and the related `tools/*_check.*` file if one exists.
3. Read `CONTRIBUTING.md` section 6 ("Where things go") to pick the location. Examples: blocks in `src/block/`, items in `src/item/`, commands in `src/command/defaults/` plus registration in `src/command/Defaults.cpp`, packets in `src/network/protocol/`, generators in `src/world/generator/`.
4. Before changing an interface, grep every use of it, including `src/plugin/Bindings*.cpp` (Lua API) and `tools/`.
5. Check whether an existing helper or abstraction already does the job before adding one.

## Making changes
- Preserve existing behavior unless the task is to change it. Do not refactor beyond the task.
- Match the style of the file being edited (`CONTRIBUTING.md`, "Naming and style").
- Handle errors the way the surrounding code does (return codes in the C core, checked results and logging via `utils::MainLogger` in C++). Check bounds and null before reading wire data.
- Code that runs off the main thread must follow the threading notes in `Architecture.md`. Do not add work to signal handlers.
- Do not touch `third_party/`.

## How to trace code (practical recipes)
- Symbol uses: `grep -rn "Symbol" src tools`; include `src/plugin/Bindings*.cpp` when the symbol is user-visible.
- Packet flow: `network/Network.cpp` -> `PacketPool::create` -> `Player::handleDataPacket` (`player/PlayerNetwork.cpp`) -> the `handle*` method.
- Tick flow: `Server::tick()` phases (see `Architecture.md`); find who calls your code and from which thread.
- Registration: blocks `BlockRegistry.cpp`, items `ItemRegistry.cpp`, commands `command/Defaults.cpp`, tiles `TileFactory.cpp`. A new class that is not registered is dead code.
- Config flow: `utils/DefaultConfig.cpp` (defaults) -> `Server::loadConfig` (reads) -> field consumers.
- Lua flow: `plugin/PluginManager` -> `LuaPlugin` -> `Bindings*.cpp`.
- See `Extension-points.md` for the exact steps for each kind of addition.

## Decision rules
- Prefer the smallest change that fits the existing design; do not introduce an abstraction until you have read the existing one it would replace.
- If code and docs disagree, trust the code, then report the discrepancy (and fix the doc only if it is part of the task).
- If a request would break a documented rule (for example a C file depending on C++), say so and propose the compliant alternative.
- Do not touch world data, `plugins/`, `data_pl/`, `server.properties` or `cursestone.yml` of a user's running server unless asked.
- Do not run `git commit`/`git push`/`git reset` without explicit instruction; the zip snapshot audited had no `.git`.

## Validation
Follow `Testing.md`. Build what you changed, run the relevant checks, review compiler output, and read the diff for regressions.

## Reporting honestly
State plainly what was done, what was verified and how, what was not run and why, and what is incomplete. Never describe a partial system as finished.

## Final review (before declaring completion)
Check the diff for: diagnostic comments; Spanish comments you added; overlong comments; missing Doxygen on new public/protected declarations; unnecessary or missing TODO markers; incomplete work presented as done; leftover debug output or instrumentation; inconsistent naming or formatting; duplication; dead code you introduced; unrelated edits; missing tests; outdated docs. Fix what your task introduced. Do not start repo-wide cleanups from a small task. Then update `changes.md` and delete temporary files you created (never pre-existing user files).
