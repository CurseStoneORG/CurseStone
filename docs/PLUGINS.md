# CurseStone Plugin API

CurseStone plugins live in `plugins/<Name>/` and are discovered by their
`plugin.yml` manifest. Two backends share the same manifest, lifecycle,
data sandbox and permission rules:

| Backend | `main` points at          | Runtime                     |
| ------- | ------------------------- | --------------------------- |
| Lua     | `src/main.lua` (script)   | Bundled Lua 5.4 interpreter |
| Native  | `HelloNative.so` / `.dll` | C ABI shared library        |

The plugin API version is `CURSESTONE_API` in `src/CurseStone.h`
(currently `1.0.3`). Plugins declare it in `plugin.yml` with the `api`
field: same major required, server minor must be >= plugin minor.

## Manifest (`plugin.yml`)

```yaml
name: MyPlugin          # identity; duplicates (case-insensitive) disable all copies
main: src/main.lua      # Lua: entry script inside src/ — or MyPlugin.so/.dll for native
api: 1.0.3              # required server plugin API
version: 1.0.0
author: YourName
```

Every plugin gets a private data folder `<server>/data_pl/<Name>/`
created at load; `data.*` (Lua) and `data.*` (native) are jailed to it
(relative paths, no `..`, no backslashes).

## Lifecycle (both backends)

```
load()    manifest + entry ready; failure discards the plugin
enable()  onEnable hook; failure disables and discards
disable() onDisable hook + listeners removed + tasks cancelled (idempotent)
unload    /plugins reload or shutdown; commands unregistered first
```

## Lua plugins

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
- `world(name)` handles — getName, getSeed, getDimension, getTime,
  setTime, getBlock, setBlock, getSpawn, getPlayers, unload
- `commands.register(name, opts, fn)`, `commands.reply(sender, msg)`
- `events.on(name, fn)` — PlayerJoin, PlayerQuit, PlayerChat, PlayerMove,
  PlayerDropItem, PlayerItemHeld, BlockBreak, BlockPlace, EntityDamage,
  EntityDamageByEntity, PlayerDeath, PlayerRespawn, PlayerBedEnter
  (alias `PlayerSleepInBed`), PlayerBedLeave, DoorToggle, PlayerInteract,
  InventoryOpen, InventoryClick, InventoryClose
- `scheduler.later/every/cancel`
- `data.read/write/exists/delete/list/loadYml/saveYml/loadJson/saveJson`
- `plugin.include/import/getResource/saveResource/listResources`

Sample plugin: `plugins/SpawnProtection/`.

## Native plugins (C++ via .so / .dll)

A native plugin is a shared library exporting ONE symbol:

```c
const CsPluginVTable* CurseStonePlugin_entry(void);
```

`src/plugin/NativeAPI.h` is the complete SDK header — copy it (or point
`-I` at the server `src/`) and compile against it. The plugin links
nothing: the entire server surface arrives as function-pointer groups in
the `CsPluginApi*` handed to `onLoad`.

```c
#include "plugin/NativeAPI.h"

extern "C" {

static void helloCmd(const CsCommandSender* s, int argc, const char* const* argv, void* ud) {
    g_api->commands->reply(s, "hi from C++");
}

CS_PLUGIN_EXPORT int myOnLoad(CsPluginApi* api) {
    api->events->on(api, "PlayerJoin", onJoin, nullptr);
    api->commands->add(api, "hello", "Greet", "/hello", nullptr, helloCmd, nullptr);
    api->scheduler->every(api, 100, heartbeat, nullptr);
    return 0;
}

CS_PLUGIN_EXPORT const CsPluginVTable* CurseStonePlugin_entry(void) {
    static const CsPluginVTable vt = {CS_PLUGIN_ABI_VERSION, myDesc, myOnLoad,
                                      myOnEnable, myOnDisable, myOnUnload};
    return &vt;
}

} // extern "C" — keeps the entry symbol's C name (no C++ mangling)
```

Build:

```sh
gcc -shared -fPIC -O2 -I<repo>/src -o MyPlugin.so src/MyPlugin.cpp   # Linux/Android
cl /LD /O2 /I<repo>\src src\MyPlugin.cpp                             # Windows
```

Rules and guarantees:

- **ABI version**: `CS_PLUGIN_ABI_VERSION` (currently 1) is checked right
  after the entry lookup; a mismatch refuses the library with a clear
  log line. It only moves when a struct layout changes — API additions
  ride the normal plugin API version.
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
- **Cross-platform manifests**: a `main` of `MyPlugin.so` also tries
  `MyPlugin.dll` on Windows (and the reverse on POSIX), so one manifest
  serves every platform.

Reference implementation: `plugins/HelloNative/` (build with the
`cursestone-example-plugin` CMake target or the one-liner above).

## Commands

- `/plugins` — lists every plugin; native ones show a `[C++]` tag.
- `/plugins reload` — disables, unloads (dtor runs `onUnload`, library
  closed) and re-scans the folder; runs on the main thread.
