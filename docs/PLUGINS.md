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
  giveItem, setNameTag, getNameTag, setDisplayName, getDisplayName,
  getPosition, getRotation, setRotation, setMotion, kill
- `server.getWorld(name)` / `server.loadWorld(name, gen)` /
  `server.defaultWorld()` — world handles with `name`, `seed`,
  `dimension`, `getTime`, `setTime`, `getBlock`, `setBlock`, `getSpawn`,
  `getPlayers`, `spawnParticle`, `spawnBlockParticles`,
  `spawnSplashParticles`, `unload`
- `commands.register(name, opts, fn)`, `commands.reply(sender, msg)`
- `events.on(name, fn)` — PlayerJoin, PlayerQuit, PlayerChat, PlayerMove,
  PlayerDropItem, PlayerItemHeld, BlockBreak, BlockPlace, EntityDamage,
  EntityDamageByEntity, PlayerDeath, PlayerRespawn, PlayerBedEnter
  (alias `PlayerSleepInBed`), PlayerBedLeave, DoorToggle, PlayerInteract,
  InventoryOpen, InventoryClick, InventoryClose
- `scheduler.later/every/cancel`
- `data.read/write/exists/delete/list/loadYml/saveYml/loadJson/saveJson`
- `plugin.include/import/getResource/saveResource/listResources`

### Particles

Protocol 84 has no arbitrary-particle packet; particles travel inside
`LevelEventPacket` world events. The Lua world handles expose the three
event shapes the 0.15.10 client actually renders:

```lua
local w = server.defaultWorld()

-- one generic particle: evid = 0x4000 | type
w.spawnParticle(x, y, z, "heart", 1)   -- by name (32 names, see below)
w.spawnParticle(x, y, z, 2, 2)         -- or by raw id 1..32

-- block break burst + dig sound in one event (2001)
w.spawnBlockParticles(bx, by, bz, blockId, meta)

-- splash-potion shatter: colored swirl + glass smash sound (2002)
w.spawnSplashParticles(x, y, z, 0xFF0000)   -- 0xRRGGBB
```

`data` payload per particle family:

- `smoke`, `white_smoke`, `ink`, `critical`, `heart`, `redstone` — scale /
  lifetime integer (`0` is the client default)
- `dust`, `mob_spell`, `mob_spell_ambient`, `mob_spell_instant` — packed
  ARGB (`0xFF______` is sent for splash; for spell types use the plain
  RGB passed in `data`)
- `terrain`, `item_break` — block/item packing `id | (meta << 12)`
- everything else — `0`

Full name list: `bubble critical smoke explode white_smoke flame lava
large_smoke redstone item_break snowball_poof large_explode huge_explode
mob_flame heart terrain town_aura portal water_splash water_wake
drip_water drip_lava dust mob_spell mob_spell_ambient mob_spell_instant
ink slime rain_splash villager_angry villager_happy enchantment_table`.
Unknown names / ids return `false` and never raise.

Sample plugin: `plugins/SpawnProtection/`.

A folder whose `main` points at a `.so`/`.dll` is **not** loaded: native
plugins are bare files now, and the log explains the migration (move the
library into `plugins/`, delete the manifest).

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
| `loadWorld(name, generator)` / `unloadWorld(name)` | unload fails for the default world |

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
| `setBlock(world, x, y, z, id, meta)` | forces the chunk resident first |
| `getSpawn(world, out[3])` / `setSpawnPoint(world, x, y, z)` | compass spawn |
| `getWeather -> 0..2` / `setWeather(world, state)` | 0 clear / 1 rain / 2 thunder |
| `spawnLightning(world, x, y, z, fire)` | |
| `isChunkLoaded(world, cx, cz)` | chunk-residency probe |
| `save(world)` | persists dirty chunks + level.dat; costly — schedule it, do not spam it |
| `spawnItem(world, x, y, z, id, count, meta)` | visible drop; count 1..64 |
| `spawnParticle(world, x, y, z, type, data)` | one generic particle, `type` = `CsParticleType` 1..32; see the Lua particles table for `data` packing |
| `spawnBlockParticles(world, x, y, z, blockId, meta)` | break burst + dig sound (LevelEvent 2001) |
| `spawnSplashParticles(world, x, y, z, rgb)` | splash shatter (LevelEvent 2002); `rgb` 0xRRGGBB |
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
