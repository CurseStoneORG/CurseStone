# HelloNative

Reference **native C++ plugin** for CurseStone. It demonstrates the C ABI
from `src/plugin/NativeAPI.h` with the smallest complete example:

- the full lifecycle: `onLoad` / `onEnable` / `onDisable` / `onUnload`
- events: `PlayerJoin`, `PlayerChat`
- a public command: `/hello [name]`
- a repeating scheduler task (heartbeat every 100 ticks)
- a write through the per-plugin data sandbox (`data_pl/HelloNative/`)

## Structure

A native plugin is a **bare shared library file** — there is no
`plugin.yml` and no folder requirement. Everything the server needs to
know (name, version, author, API) comes from the `CsPluginDesc` the
vtable returns. This `src/` tree is just the example's source; the
loadable artifact is the single file produced below.

## Build

The plugin links **nothing** — every call goes through the
`CsPluginApi` function-pointer groups the server hands to `onLoad`.

Linux / macOS / Android NDK:

```sh
gcc -shared -fPIC -O2 -I<repo>/src -o HelloNative.so src/HelloNative.cpp
```

Windows (MSVC, x64):

```sh
cl /LD /O2 /I<repo>\src src\HelloNative.cpp
```

CMake target (same output file, kept out of the default build):

```sh
cmake --build <build-dir> --target cursestone-example-plugin
```

## Install

Drop the built file **directly** into the server's `plugins/` folder:

```
plugins/
  HelloNative.so       # Linux / macOS / Android
  HelloNative.dll      # Windows
```

That is the whole installation — no folder, no manifest. On Linux a
`HelloNative.dll` is skipped with a hint (and the reverse on Windows):
each platform loads its own extension, and both files may sit side by
side for a single cross-platform distribution.

## Verify

After starting the server the log must contain:

```
[HelloNative] onLoad: server API 1.0.3, data dir ...
[HelloNative] heartbeat 1 (tick ...)
Plugin enabled: HelloNative v1.0.0 [native] by Dr1xyDev
```

`/plugins` lists it as `HelloNative v1.0.0 by Dr1xyDev [C++]`, and
`/hello` answers with live TPS/MSPT stats and your gamemode.
