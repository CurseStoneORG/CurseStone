# CurseStone Plugin API

CurseStone plugins come in two load types, distinguished by what an
operator puts into the `plugins/` folder:

| Load type | What is dropped into `plugins/` | Manifest | Runtime |
| --------- | ------------------------------- | -------- | ------- |
| **Folder** | `MyPlugin/` — a folder with a `plugin.yml` | `plugin.yml` | Bundled Lua 5.4 interpreter |
| **File** | `MyPlugin.so` / `MyPlugin.dll` — a bare shared library | none — the binary IS the plugin | Native C++ shared library |

The rules in one line each:

- **Lua plugins are folder plugins**: `plugins/<Name>/plugin.yml` with the
  entry script under `src/`.
- **C++ plugins are file plugins**: drop the compiled `.so` (Linux,
  Android, macOS) or `.dll` (Windows) directly into `plugins/`. No folder,
  no `plugin.yml`, nothing else — the server opens the library, reads the
  descriptor inside it and that is the whole identity.

The plugin API version is `CURSESTONE_API` in `src/CurseStone.h`
(currently `1.0.3`).

## Folder plugins (Lua)

```yaml
# plugins/MyPlugin/plugin.yml
name: MyPlugin          # identity; duplicates (case-insensitive) disable all copies
main: src/main.lua      # entry script inside src/
api: 1.0.3              # required server plugin API
version: 1.0.0
author: YourName
```

Entry `src/main.lua` must return a table with optional `onEnable` /
`onDisable`. The VM is sandboxed: no `io`, `os`, `package` or `debug`,
and per-plugin instruction/time limits trip a circuit breaker.

API surface (globals):

- `log.info/warn/error(msg)`
- `server.broadcast(msg)`, `server.players()`, `server.getPlayer(name)`
- `player.*` — message, tip, popup, kick, teleport, teleportWorld,
  getWorld, isOp, getHealth, setHealth, setPermission, hasPermission,
  giveItem, getHeldItem, setNameTag, getNameTag, setDisplayName,
  getDisplayName, getPosition, getRotation, setRotation, setMotion, kill
- `server.getWorld(name)` / `server.loadWorld(name, gen, [seed])` /
  `server.unloadWorld(name)` / `server.isWorldLoaded(name)` /
  `server.defaultWorld()` — world handles with `name`, `seed`,
  `dimension`, `generator`, `isDefault`, `getTime`, `setTime`, `getBlock`,
  `getBlockMeta`, `setBlock`, `fill`, `getSpawn`, `setSpawn`, `getPlayers`,
  `playerCount`, `isChunkLoaded` (chunk coords), `chunkCount`, `save`,
  `spawnParticle`, `spawnBlockParticles`, `spawnSplashParticles`,
  `spawnFloatingText`, `moveFloatingText`, `setFloatingText`,
  `setFloatingTextTitle`, `setFloatingTextBody`, `setFloatingTextVisible`,
  `setFloatingTextFollow`, `setFloatingTextFollowPlayer`,
  `setFloatingTextViewers`, `getFloatingTexts`, `removeFloatingText`,
  `spawnEntity`, `moveEntity`, `removeEntity`, `getEntity`, `getEntities`,
  `entityTypes`, `unload`. World names must be 1..64 letters, digits,
  `_` or `-` (they become directory names); generators are `normal`,
  `flat`, `nether`, `void` or `empty` (case-insensitive). New secondary
  worlds persist in `worlds/worlds.manifest` and reload on the next boot;
  unloading removes the entry.

### Building blocks from plugins

Every block write a plugin makes is visible to clients immediately —
`world.setBlock` refreshes all viewers of the cell with one
UpdateBlockPacket, so there is no "the block only appears after the chunk
re-loads" window. For WorldEdit-style volume work use the bulk write:

```lua
local w = server.defaultWorld()
local n = w.fill(x1, y1, z1, x2, y2, z2, 41, 0)  -- gold box between corners
-- n = cells actually written; -1 = volume above the vanilla cap (32768)
-- no-op cells (same id AND data) cost nothing; corners may be given in
-- any order; y is clamped into the world
```

Heavily modified chunks reach their viewers as one whole-chunk re-send
(one packet per 4096 cells), lightly touched chunks as per-block packets;
the box boundary gets the vanilla neighbor wake so liquids and sand meet
the new walls. `world.getBlockMeta(x, y, z)` reads the meta nibble back
(wood types, wool colors, facing). `player.getHeldItem(name)` returns
`id, meta, count, customName` (customName nil when unnamed) — how a tool
plugin identifies its own custom-named brush item.
- `commands.register(name, opts, fn)`, `commands.reply(sender, msg)`
- `events.on(name, fn)` — PlayerJoin, PlayerQuit, PlayerChat, PlayerMove,
  PlayerDropItem, PlayerItemHeld, BlockBreak, BlockPlace, EntityDamage,
  EntityDamageByEntity, PlayerDeath, PlayerRespawn, PlayerBedEnter
  (alias `PlayerSleepInBed`), PlayerBedLeave, DoorToggle, PlayerInteract,
  InventoryOpen, InventoryClick, InventoryClose, WorldLoad (e.world name,
  e.dimension), WorldUnload (cancellable, e.world name)
- `scheduler.later/every/cancel`
- `data.read/write/exists/delete/list/loadYml/saveYml/loadJson/saveJson`
- `plugin.include/import/getResource/saveResource/listResources`

### Particles

Protocol 84 has no arbitrary-particle packet; particles travel inside
`LevelEventPacket` world events. The Lua world handles expose the three
event shapes the 0.15.10 client actually renders, plus floating text:

```lua
local w = server.defaultWorld()

-- one generic particle: evid = 0x4000 | type
w.spawnParticle(x, y, z, "heart", 1)   -- by name (32 names, see below)
w.spawnParticle(x, y, z, 2, 2)         -- or by raw id 1..32

-- block break burst + dig sound in one event (2001)
w.spawnBlockParticles(bx, by, bz, blockId, meta)

-- splash-potion shatter: colored swirl + glass smash sound (2002)
w.spawnSplashParticles(x, y, z, 0xFF0000)   -- 0xRRGGBB

-- floating text (FloatingTextParticle): ONE invisible fake-player
-- carrier whose nametag is the whole text (this client era draws
-- nametags only on player entities, and protocol 84 has no text
-- particle). The Genisys tag model: the first '\n' line is the TITLE
-- and the rest the BODY, and the client stacks them itself, so a text
-- costs one entity no matter how many lines it carries. Returns the
-- entity id, or nil at the per-world cap (128/world).
local id = w.spawnFloatingText(x, y, z, "Welcome\n§7to the hub", 200)
w.moveFloatingText(id, x, y + 1, z)         -- one MovePlayerPacket
w.setFloatingText(id, "New title\n§anew body") -- one metadata packet resyncs
w.setFloatingTextTitle(id, "Top")           -- Genisys setTitle: one metadata packet
w.setFloatingTextBody(id, "§7lines below")  -- Genisys setText: one metadata packet
w.setFloatingTextVisible(id, false)         -- hide: one Remove per viewer
w.setFloatingTextVisible(id, true)          -- show: the audience re-arms now
w.setFloatingTextFollow(id, entityEid)      -- tag rides above an entity/mob
w.setFloatingTextFollowPlayer(id, "Steve")  -- tag rides above a player
w.setFloatingTextViewers(id, {"Steve"})     -- allow list; nil = everyone
for _, t in ipairs(w.getFloatingTexts()) do -- {id,x,y,z,text,title,body,visible,follow}
end
w.removeFloatingText(id)                    -- one RemoveEntityPacket
```

`spawnFloatingText(x, y, z, text, [ttlTicks])`: `ttlTicks > 0` despawns
automatically after that many ticks, `0` keeps the text until
`removeFloatingText` (entity ids do not survive restarts). The title is
capped at 128 chars and the whole pair at 512; the invisible carrier
hangs 1.62 below the text line so the nametag anchor lands at the
requested spot.

`setFloatingText(id, text)` replaces the content with the same
multi-line model (first line = title, rest = body). Only the nametag
moves on the wire: one SetEntityDataPacket resyncs every viewer — an
animated title costs one packet per step, not a respawn.
`setFloatingTextTitle` / `setFloatingTextBody` replace one half of the
Genisys pair and cost at most the same one metadata packet.

`getFloatingTexts()` returns `{id,x,y,z,text,title,body,visible,follow}`
tables ordered by entity id (`text` is the whole nametag with `\n`
separators; `follow` is the anchored entity id or 0).

#### Tag types (all ride the same one-carrier model)

| Type | Call | Wire cost |
| ---- | ---- | --------- |
| Static | `spawnFloatingText` (default) | one fixed spawn pair |
| Follower | `setFloatingTextFollow(id, eid)` / `...FollowPlayer(id, name)` | one MovePlayerPacket only when the target moves past 0.15 blocks; auto-removed when the target despawns |
| Filtered | `setFloatingTextViewers(id, {"A", "B"})` (nil = everyone) | none — only listed players are ever served |
| Hidden | `setFloatingTextVisible(id, false)` / `true` | one Remove per viewer to hide; the show re-arms immediately |
| Expiring | `spawnFloatingText(..., ttlTicks)` | none — the scheduler despawns it |

Viewer churn reconciles once per second: players that join or walk into
render range receive the text, players that walk out, quit or switch
worlds stop rendering it — the same canSeeBlock audience every other
world broadcast uses. Between syncs the only per-tick cost of a text is
none: spawn, move, re-text and removal are the only packet moments.

#### Why this cannot hurt the FPS

- One text = one entity whose only visible part is its nametag: the
  client cost is a player nametag, and the 128/world cap bounds the
  worst case.
- Every packet is cached per text and shared by every viewer — a
  re-text is ONE metadata packet for everyone, a move ONE move packet.
- Spawn faces rebuild lazily (dirty flag): a stream of moves or re-texts
  never rebuilds what no viewer is reading.
- Follower texts resync past a 0.15-block threshold only; a still target
  costs zero packets per tick.
- The follow pass and the viewer sync touch only their subset: a world
  without floating texts pays nothing, and follow texts cost nothing
  while their target stands still.

`data` payload per particle family (each particle type owns its packing
in its own file under `src/world/particle/`):

- `smoke`, `white_smoke`, `ink`, `critical`, `heart`, `redstone` — scale /
  lifetime integer (`0` is the client default)
- `dust`, `mob_spell`, `mob_spell_ambient`, `mob_spell_instant` — packed
  ARGB (`0xFF______` is sent for splash; for spell types use the plain
  RGB passed in `data`)
- `item_break` — item packing `(itemId << 16) | damage`; the item id must
  be a registered item (stick is 280, so `280 * 65536`). Payloads the
  client cannot render — item id 0 (what the old low-bits packing
  produced) or unregistered ids — return `false`: they crash the
  0.15.10 renderer.
- `terrain` — block packing `(meta << 8) | id`; air (id 0) returns
  `false` for the same reason.
- everything else — `0`

Payloads a client cannot render never reach the wire: `spawnParticle`
returns `false` instead of broadcasting, so a bad `data` can never crash
a player.

Full name list: `bubble critical smoke explode white_smoke flame lava
large_smoke redstone item_break snowball_poof large_explode huge_explode
mob_flame heart terrain town_aura portal water_splash water_wake
drip_water drip_lava dust mob_spell mob_spell_ambient mob_spell_instant
ink slime rain_splash villager_angry villager_happy enchantment_table`.
Unknown names / ids return `false` and never raise.

Sample plugin: `plugins/SpawnProtection/`.

Test plugin for the floating text API: `plugins/FloatingTextDemo/`
drives every call through the `/ft` command.

A folder whose `main` points at a `.so`/`.dll` is **not** loaded: native
plugins are bare files now, and the log explains the migration (move the
library into `plugins/`, delete the manifest).

### Entities (standing mobs)

World handles expose the mob registry the creative spawn eggs use. A
spawned mob has no AI and no physics: it spawns exactly where the caller
puts it and stays there. Cost is three packets in its whole lifetime —
one AddEntityPacket on spawn, one MoveEntityPacket per move, one
RemoveEntityPacket on removal — with no per-tick entry in the world loop,
and viewers that stream its chunk later (joins, walk-ins) receive it from
the chunk stream, deduplicated per client.

```lua
local w = server.defaultWorld()

local eid = w.spawnEntity("zombie", x, y, z)      -- name or raw id (see below)
w.spawnEntity("cow", x, y, z, 90.0)               -- + yaw
w.moveEntity(eid, x, y + 1, z)                    -- one MoveEntityPacket
local e = w.getEntity(eid)                        -- {id,type,name,x,y,z,yaw,pitch}
for _, m in ipairs(w.getEntities()) do ... end    -- every standing mob
w.removeEntity(eid)                               -- despawn
w.entityTypes()                                   -- name -> protocol-84 id map
```

`type` accepts the canonical lowercase names — `chicken cow pig sheep
wolf villager mooshroom squid rabbit bat ocelot horse donkey mule
skeleton_horse zombie_horse zombie creeper skeleton spider
zombie_pigman slime enderman silverfish ghast lava_slime magma_cube
blaze zombie_villager witch stray husk wither_skeleton` — or their
protocol-84 network id (10..48 range above; on this protocol the spawn
egg aux value IS the id). Common aliases (`mushroom_cow`, `pigman`,
`cat`, `magma`, ...) resolve too. Unknown names/ids, out-of-height
positions and the per-world cap (256) return `false` and never raise.
Every id in the registry has its own egg in the 0.15.10 creative
inventory, which is what makes the client's renderer for it
client-verified — ids outside the registry never reach the wire.

Entity ids do not survive restarts, like floating text ids.

## File plugins (C++ via .so / .dll)

### Structure

The final plugin is ONE file, named after the plugin:

```
plugins/
  SpawnProtection/          <- Lua plugin: folder + plugin.yml
    plugin.yml
    src/main.lua
  HelloNative.so            <- C++ plugin: the file itself
  HelloNative.dll           <- its Windows flavor (optional, may sit side by side)
```

- **No `plugin.yml`**: name, version, author and the API the plugin was
  built for come from the `CsPluginDesc` the vtable returns.
- **No folder**: the file sits directly in `plugins/`. The file **stem**
  (`HelloNative` from `HelloNative.so`) owns the private data folder
  `data_pl/<stem>/` and doubles as the fallback display name.
- **Cross-platform flavors**: `HelloNative.so` and `HelloNative.dll` may
  sit side by side; each platform picks its own extension. A lone
  foreign-extension file is skipped with a hint (`MyPlugin.dll` on Linux,
  `MyPlugin.so` on Windows) instead of a cryptic loader error.
- **Naming rules**: the stem must be unique per server (case-insensitive),
  and the descriptor `name` must not collide with another loaded plugin —
  duplicates disable the copies involved with a warning.

The library exports ONE symbol:

```cpp
const CsPluginVTable* CurseStonePlugin_entry(void);
```

`src/plugin/NativeAPI.h` is the complete SDK header — copy it (or point
`-I` at the server `src/`) and compile against it. The plugin links
nothing: the entire server surface arrives as function-pointer groups in
the `CsPluginApi*` handed to `onLoad`.

### Minimal complete plugin

```cpp
#include "plugin/NativeAPI.h"

extern "C" {   // required so the entry symbol is exported unmangled

static CsPluginApi* g_api = nullptr;

static void onJoin(CsEvent* ev, void* ud) {
    g_api->player->sendMessage(ev->player, "welcome!");
}

static void helloCmd(const CsCommandSender* s, int argc, const char* const* argv, void* ud) {
    g_api->commands->reply(s, "hi from C++");
}

CS_PLUGIN_EXPORT const CsPluginDesc* myDesc(void) {
    static const CsPluginDesc desc = {CS_PLUGIN_ABI_VERSION, "MyPlugin", "1.0.0",
                                      "YourName", "1.0.3"};
    return &desc;
}

CS_PLUGIN_EXPORT int myOnLoad(CsPluginApi* api) {
    g_api = api;
    api->events->on(api, "PlayerJoin", onJoin, nullptr);
    api->commands->add(api, "hello", "Greet", "/hello", nullptr, helloCmd, nullptr);
    api->scheduler->every(api, 100, heartbeat, nullptr);
    return 0;   // non-zero discards the plugin
}

CS_PLUGIN_EXPORT void myOnEnable(void)  {}
CS_PLUGIN_EXPORT void myOnDisable(void) {}
CS_PLUGIN_EXPORT void myOnUnload(void)  { g_api = nullptr; }

CS_PLUGIN_EXPORT const CsPluginVTable* CurseStonePlugin_entry(void) {
    static const CsPluginVTable vt = {CS_PLUGIN_ABI_VERSION, myDesc, myOnLoad,
                                      myOnEnable, myOnDisable, myOnUnload};
    return &vt;
}

} // extern "C"
```

`onLoad` / `desc` are required; the other hooks may be `NULL`. The
descriptor is the manifest: keep `name` non-empty and unique, and set
`api` to the `CURSESTONE_API` you built against (an empty value means
"this SDK"; an incompatible claim refuses the load).

### Building

```sh
# Linux / macOS / Android NDK
c++ -shared -fPIC -O2 -I<repo>/src -o MyPlugin.so src/MyPlugin.cpp

# Windows (MSVC, x64)
cl /LD /O2 /I<repo>\src src\MyPlugin.cpp
```

Or with CMake (see `plugins/HelloNative/` and the `cursestone-example-plugin`
target):

```cmake
add_library(MyPlugin SHARED src/MyPlugin.cpp)
target_include_directories(MyPlugin PRIVATE <repo>/src)
set_target_properties(MyPlugin PROPERTIES PREFIX "")   # MyPlugin.so, not libMyPlugin.so
```

For Android use the NDK toolchain (`-DANDROID_ABI=arm64-v8a` and friends);
for MSVC make sure the platform matches the server build (x64 vs x86).

### Data folder

Every loaded plugin gets `<server>/data_pl/<stem>/` created at load. The
`data.*` group is jailed to it: relative paths only, no `..`, no
backslashes. The Lua sandbox uses the same root, keyed by folder name.

### Rules and guarantees

- **ABI version**: `CS_PLUGIN_ABI_VERSION` (currently 1) is checked right
  after the entry lookup; a mismatch refuses the library with a clear log
  line. It only moves when an existing field changes meaning, type or
  order — new surface is appended to the end of the group structs.
- **Older/newer servers**: a plugin built against a newer header than the
  server detects that through `api->apiSize` — compare it with
  `sizeof(CsPluginApi)` as compiled before calling any appended function.
- **Threading**: every callback (events, tasks, commands) runs on the
  main thread. Never block inside a callback.
- **Safety**: the library opens with `RTLD_LOCAL` (symbol-isolated from
  other plugins); every callback is wrapped so an exception is logged,
  not fatal; the command bridge re-resolves the plugin by name and
  registration id each execution, so `/plugins reload` can never call a
  pointer from a `dlclose()`d library.
- **Context-bound groups**: `log.*`, `scheduler.*`, `commands.*`,
  `events.*` and `data.*` take the `CsPluginApi*` first; `server.*`,
  `player.*` and `world.*` are context-free.
- **Permissions** mirror the Lua `commands.register` vocabulary:
  `NULL`/`""` = public, `"op"` = operators + console, any other string =
  permission node.
- **String ownership**: strings handed OUT by the API are valid only
  until the callback returns; buffers the plugin fills are caller-owned.

### API reference (native)

All functions return `bool`/`int` failure markers (`false` / `-1` / `0`)
instead of raising anything; invalid arguments never crash the server.

**log** (context-bound)

| Function | Notes |
| -------- | ----- |
| `info/warn/error(api, fmt, ...)` | printf-style, `[PluginName]` prefix; malformed formats are logged as-is |

**scheduler** (context-bound; tasks run on the main thread, cancelled on disable)

| Function | Notes |
| -------- | ----- |
| `later(api, ticks, fn, ud) -> id` | one-shot; `0` on invalid input |
| `every(api, ticks, fn, ud) -> id` | repeating |
| `cancel(api, id) -> bool` | |

**commands** (context-bound; names case-insensitive, stored lowercase)

| Function | Notes |
| -------- | ----- |
| `add(api, name, description, usage, permission, fn, ud) -> bool` | permission: `NULL`/`""` public, `"op"`, or a node |
| `reply(sender, msg)` | console → log, player → chat |

**events** (context-bound; names identical to the Lua `events.on()` list above)

| Function | Notes |
| -------- | ----- |
| `on(api, eventName, fn, ud) -> bool` | `false` on unknown names; writable fields: `message`, `amount`, `cancelled` |

**server** (context-free)

| Function | Notes |
| -------- | ----- |
| `broadcast(msg)` | chat line to every player |
| `broadcastTip(msg)` / `broadcastPopup(msg)` | tip/popup flavor to every player |
| `playerCount()` / `playerNameAt(idx, out, cap)` / `isPlayerOnline(name)` | player list queries |
| `tps() -> 0..20` / `mspt() -> double` / `currentTick() -> u64` | tick statistics |
| `dispatchCommand(line) -> bool` | runs `/line` as console |
| `motd(out, cap)` | configured MOTD |
| `maxPlayers()` / `difficulty()` / `whitelistEnabled()` | server.properties reads |
| `isBanned(name)` / `setBanned(name, value)` | name bans; banning kicks the player |
| `worldCount()` / `worldNameAt(idx, out, cap)` / `worldExists(name)` | loaded-world queries |
| `loadWorld(name, generator)` / `unloadWorld(name)` | unload fails for the default world; names/generators are validated, new worlds persist in the manifest |
| `defaultWorldName(out, cap)` | default (spawn) world name — batch 9 |

**player** (context-free; every call fails for offline players)

| Function | Notes |
| -------- | ----- |
| `sendMessage` / `sendTip` / `sendPopup(name, msg)` | chat flavors |
| `kick(name, reason)` / `kill(name)` | |
| `getHealth -> 0..20` / `setHealth(name, hp)` | hearts |
| `getGamemode -> 0..3` / `setGamemode(name, gm)` | 0 survival / 1 creative / 2 adventure / 3 spectator |
| `addEffect(name, effectId, durationTicks, amplifier)` / `removeEffect(name, effectId)` | vanilla ids, Speed=1 .. Saturation=23 |
| `getExperienceLevel -> int` / `setExperience(name, exp, level)` | exp is 0..100 progress percent |
| `getAddress(name, out, cap)` | `"ip:port"` |
| `teleport(name, x, y, z)` / `teleportWorld(name, world, x, y, z)` | |
| `getWorld(name, out, cap)` / `getPosition(name, out[3])` | |
| `getRotation(name, out[2])` / `setRotation(name, yaw, pitch)` | camera re-sync included |
| `setMotion(name, x, y, z)` | protocol-84 self-motion form |
| `setPermission(name, node, value)` / `hasPermission(name, node)` | |
| `giveItem(name, id, count, meta, customName)` | count ≤ 2304; full inventory resync |
| `setNameTag` / `getNameTag` / `setDisplayName` / `getDisplayName` | |
| `getSpawnPoint(name, out[3])` / `setSpawnPoint(name, x, y, z)` | personal spawn, persisted |
| `setOnFire(name, seconds)` | `0` extinguishes |

**world** (context-free; fails for unloaded worlds)

| Function | Notes |
| -------- | ----- |
| `seed(world) -> i64` / `dimension(world) -> int` | |
| `getTime -> int` / `setTime(world, t)` | normalized to 0..23999 |
| `getBlock(world, x, y, z) -> id` / `getBlockMeta(...) -> 0..15` | `0` for unloaded chunks / invalid y |
| `setBlock(world, x, y, z, id, meta)` | forces the chunk resident first; refreshes every viewer of the cell |
| `fillBlocks(world, x1, y1, z1, x2, y2, z2, id, meta)` | bulk /fill write, vanilla 32768-cell cap; cells written, -1 over cap — batch 10 |
| `getSpawn(world, out[3])` / `setSpawnPoint(world, x, y, z)` | compass spawn |
| `getWeather -> 0..2` / `setWeather(world, state)` | 0 clear / 1 rain / 2 thunder |
| `spawnLightning(world, x, y, z, fire)` | |
| `isChunkLoaded(world, cx, cz)` | chunk-residency probe |
| `chunkCount(world) -> int` | resident chunks (cache size), -1 unloaded — batch 9 |
| `save(world)` | persists dirty chunks + level.dat; costly — schedule it, do not spam it |
| `spawnItem(world, x, y, z, id, count, meta)` | visible drop; count 1..64 |
| `spawnParticle(world, x, y, z, type, data)` | one generic particle, `type` = `CsParticleType` 1..32; see the Lua particles table for `data` packing; unrenderable payloads return 0, never broadcast |
| `spawnBlockParticles(world, x, y, z, blockId, meta)` | break burst + dig sound (LevelEvent 2001) |
| `spawnSplashParticles(world, x, y, z, rgb)` | splash shatter (LevelEvent 2002); `rgb` 0xRRGGBB |
| `spawnFloatingText(world, x, y, z, text, ttlTicks)` | floating text (one invisible fake-player carrier; first `\n` line = title, rest = body, client-side stacked); entity id, or -1 at the cap; ttl 0 = until removed |
| `moveFloatingText(world, id, x, y, z)` | repositions a floating text |
| `setFloatingText(world, id, text)` | re-texts a floating text (one metadata packet); false for unknown ids |
| `setFloatingTextTitle(world, id, title)` | Genisys setTitle: replaces the top line; one metadata packet |
| `setFloatingTextBody(world, id, text)` | Genisys setText: replaces the body lines; one metadata packet |
| `setFloatingTextVisible(world, id, visible)` | hide (one Remove per viewer) / show (re-arm now) |
| `setFloatingTextFollow(world, id, targetEid)` | anchor over one entity/player (0 = fixed spot); one move packet past the threshold, auto-remove on target loss |
| `setFloatingTextViewers(world, id, names, count)` | viewer allow list (NULL/0 = everyone); dropped viewers despawn at once |
| `removeFloatingText(world, id)` | despawns a floating text; 0 for unknown ids |
| `getFloatingTexts(world, out, max)` | copies `{id, x, y, z, text}` snapshots (ordered by id) into a `CsFloatingTextInfo` array; returns how many were written |
| `getFloatingTextEx(world, id, out)` | one-text snapshot with the Genisys pair separated (`CsFloatingTextInfoEx`: title, body, visible, follow, viewers) |
| `spawnEntity(world, type, x, y, z, yaw, pitch)` | standing mob (no AI, zero per-tick cost); entity id, or -1 for an invalid type/position or at the cap; y is feet |
| `moveEntity(world, id, x, y, z, yaw, pitch)` | repositions a standing mob |
| `removeEntity(world, id)` | despawns a standing mob; 0 for unknown ids |
| `getEntity(world, id, outType, outPos[3], outRot[2])` | reads a mob back (type, feet position, yaw/pitch) |
| `entityTypeFromName(name, outId)` / `entityTypeName(type)` | the shared mob registry, same names the Lua `entityTypes()` maps |
| `playerCount(world)` | players currently in that world |

**data** (context-bound; jailed to `data_pl/<stem>`)

| Function | Notes |
| -------- | ----- |
| `exists(api, rel)` / `remove(api, rel)` | |
| `readFile(api, rel, void** out, uint32_t* size)` | server-allocated buffer; release with `data.free` |
| `writeFile(api, rel, data, size)` | creates parent folders |
| `list(api, rel, out, cap) -> count` | entry names joined with `\n` |
| `free(ptr)` | releases `readFile` buffers across allocator boundaries |

### Versioning

`CS_PLUGIN_ABI_VERSION` stays at 1 while fields are only appended; the
server's plugin API version (`CURSESTONE_API`, currently `1.0.3`) only
moves at release time, not per change.

## Commands

- `/plugins` — lists every plugin; native ones show a `[C++]` tag.
- `/plugins reload` — disables, unloads (dtor runs `onUnload`, library
  closed) and re-scans the folder (folders for Lua, bare files for C++),
  all on the main thread.

## Reference implementation

`plugins/HelloNative/` — build with the `cursestone-example-plugin`
CMake target or the one-liner above, then drop the produced
`HelloNative.so` / `HelloNative.dll` directly into `plugins/`.
