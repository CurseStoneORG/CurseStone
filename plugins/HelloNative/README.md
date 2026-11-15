# HelloNative

Reference **native C++ plugin** for CurseStone. It demonstrates the C ABI
from `src/plugin/NativeAPI.h` with the smallest complete example:

- the full lifecycle: `onLoad` / `onEnable` / `onDisable` / `onUnload`
- events: `PlayerJoin`, `PlayerChat`
- a public command: `/hello [name]`
- a repeating scheduler task (heartbeat every 100 ticks)
- a write through the per-plugin data sandbox (`data_pl/HelloNative/`)

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

Place the built `HelloNative.so` (or `HelloNative.dll` on Windows) next to
this `plugin.yml`:

```
plugins/
  HelloNative/
    plugin.yml          # main: HelloNative.so
    HelloNative.so      # the built library
```

On Windows the loader also accepts a `main` ending in `.so` and swaps it
to `.dll` automatically, so one manifest works on every platform.

## Verify

After starting the server the log must contain:

```
[HelloNative] onLoad: server API 1.0.3, data dir ...
[HelloNative] heartbeat 1 (tick ...)
Plugin enabled: HelloNative v1.0.0 [native] by Dr1xyDev
```

`/plugins` lists it as `HelloNative v1.0.0 by Dr1xyDev [C++]`, and
`/hello` answers with live TPS/MSPT stats.
