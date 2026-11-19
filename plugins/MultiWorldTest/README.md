# MultiWorldTest

End-to-end test plugin for the CurseStone multiworld system (Lua, API 1.0.3).

## Commands

| Command | What it does |
| --- | --- |
| `/mw status` | One line per loaded world: dimension, generator, players, resident chunks |
| `/mw create <name> [gen] [seed]` | Loads (or creates) a world; gen = `normal`/`flat`/`nether`/`void`/`empty` |
| `/mw unload <name>` | Unloads a world (players move to the default world's spawn) |
| `/mw tp <world> [x y z]` | Switches you into a world (world spawn or given coordinates) |
| `/mw save [world]` | Flushes dirty chunks + level.dat to disk |
| `/mw mark` | Builds a two-block red wool marker at your position |
| `/mw mob` | Spawns a standing zombie 3 blocks ahead (transport sweep probe) |
| `/mw mobclear` | Removes every zombie this plugin spawned in your world |
| `/mw check <name>` | The automated full sequence (below) |
| `/mw help` | The command list |

## `/mw check` — the automated sequence

Drives the whole multiworld surface and stamps one server-log line per
capability (`PCHK MW <label> ok`); any deviation stamps
`PCHK MW FAIL <label> <detail>` and cleans up:

1. **load** — `server.loadWorld(name, "flat", 424242)` + `isWorldLoaded`,
   generator/seed round trip, `WorldLoad` event fired.
2. **transport** — `player.teleportWorld` into the world; after the chunk
   stream settles: `player.getWorld` name, `playerCount`, `chunkCount`,
   arrival-chunk `isChunkLoaded`.
3. **blockset / chunkres** — `setBlock`/`getBlock` round trip on a wool
   marker + marker-chunk residency.
4. **save** — `world.save()`, then `teleportWorld` back to the default world.
5. **unload** — `server.unloadWorld` + `isWorldLoaded` false + `WorldUnload`
   event fired.
6. **reload** — the same `loadWorld` call again (the manifest entry is
   recreated).
7. **persist** — the wool marker survived the unload/reload on disk.
8. **nether** — `<name>-nether` loads with dimension 1.
9. **cleanup** — both worlds unload and stay unloaded.

Final marker: `PCHK MW PASS`.
