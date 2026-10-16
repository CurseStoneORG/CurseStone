# Testing and validation (from the actual repository)

## What exists
- No CTest and no test target in `CMakeLists.txt`; CI (`Build.yml`) builds only and runs no tests.
- `tools/` exists (about 55 files): standalone C++ checks, Python end-to-end checks, six shell runners, and comment tooling. `tools/__pycache__/` is present in the tree; do not add to it or depend on it.
- Shell runners (each compiles the real sources with `gcc -std=c2x` / `g++ -std=c++20`, writes to `/tmp`, honors `CXX`, `CC`, `OUT`): `run_nbt_test.sh`, `run_autosave_check.sh`, `run_anvil_async_check.sh`, `run_async_logger_check.sh`, `run_gen_perf.sh`, `run_vanilla_noise_test.sh`.
- Other `*_check.cpp`/`*_test.cpp` files carry a `g++ ...` recipe in a header comment; some use `-I/tmp/zstub` (a zlib stub) when zlib dev files are absent. Read the header; do not guess flags.
- Python checks (`*_check.py`, `fuzz_packets.py`, `repro_crash.py`) talk to an already running server: `--host`, `--port` (default 127.0.0.1:19132); see each docstring.
- `lint_comments.py` / `comment_inventory.py`: read-only comment tooling; they never edit files.

## Area -> existing check (by file name and header; not every one was run)
| Area | Check |
|---|---|
| NBT / item NBT | `nbt_test.cpp` via `run_nbt_test.sh` |
| Anvil / autosave / chunk IO | `autosave_check.cpp`, `anvil_async_check.cpp` (runners) |
| Logger | `async_logger_check.cpp` (runner) |
| Terrain, biomes, rivers, plains, villages, noise | `gen_smoke.cpp`, `gen_perf.cpp`, `biome_check.cpp`, `river_check.cpp`, `plains_check.cpp`, `village_check.cpp`, `vanilla_noise_test.cpp`, `cross_border_check.cpp` |
| Lighting | `light_check.cpp` |
| Containers, furnace, signs | `container_check.cpp`, `sign_tile_check.cpp`, `sign_nbt_check.cpp` |
| Recipes / creative | `recipe_check.cpp`, `dump_recipes.cpp`, `creative_check.cpp` |
| Commands / permissions | `command_node_check.cpp`, `command_permission_check.cpp`, `op_perm_test.cpp` |
| Multiplayer state, player list, wire formats | `multiplayer_flow_check.cpp`, `multiplayer_state_check.cpp`, `playerlist_wire_check.cpp`, `sleep_metadata_wire_check.cpp`, `take_item_wire_check.cpp`, `login_decode_harness.cpp`, `reliable_dup_check.cpp` |
| Combat, pickup, falling blocks, portals | `combat_pickup_anim_check.cpp`, `pickup_sim.cpp`, `falling_block_check.cpp`, `portal_check.cpp`, `portal_travel_check.py` |
| Live-server flows (hunger, fall damage, doors/beds, fire particles, login, plugin API, kill) | `hunger_check.py`, `fall_damage_check.py`, `door_bed_flow_check.py`, `fire_particles_check.py`, `linux_login_check.py`, `plugin_api_check.py`, `getplayer_kill_check.py` |
| Packet robustness | `fuzz_packets.py`, `repro_crash.py` |
`gen_village_pieces.py` generates data for a separate structures repo (see its header); do not run it casually.

## Procedure for a change
1. Identify the subsystem (`Architecture.md`) and find an existing check: `ls tools | grep -i <area>`, `grep -ln <Symbol> tools/*`.
2. Prefer existing tools. Add a new check to `tools/` only when behavior is worth protecting and none exists: follow a runner's pattern, link the real sources (not copies), and document the build recipe in the header.
3. Build:
   - With CMake: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug` then `cmake --build build --parallel` (or preset `linux-debug`). This is the only path with `-Wall -Wextra` diagnostics.
   - Without CMake: `./build_local.sh` (output `/tmp/cs_build`, override with `OUT`). It passes `-w`, so it hides warnings; never use it to claim "no new warnings".
4. Run the relevant checks. For live-server checks start `./build/CurseStone` in a throwaway directory first.
5. Read every compiler warning/error; the project expects no new warnings.
6. Smoke test (per `CONTRIBUTING.md`): server starts, accepts a client, stops cleanly with `/stop`. For performance or disconnect issues use `/workbench start` then `/workbench report`.
7. Review the diff for regressions and list anything you could not run, with the reason.

## Rules
- Never claim success unless the command completed with exit 0 and the expected output.
- Never leave temporary instrumentation in `src/`.
- Remove temp files and extra build directories you created when safe (`/tmp` outputs; `build/` is git-ignored). Never delete pre-existing user files, worlds, `plugins/` or `data_pl/`.
- Run `python3 tools/lint_comments.py <changed dir>` on touched directories and make sure you added no new findings.

## Verified in the audit sandbox
`bash tools/run_nbt_test.sh` compiled and passed ("NBT TEST: OK (0 failure(s))"). `lint_comments.py src` ran (1120 findings baseline). The sandbox has GCC 13.3 and Python 3 but no CMake, Ninja, clang-format or Doxygen, so CMake builds, formatting checks and Doxygen were not run here.
