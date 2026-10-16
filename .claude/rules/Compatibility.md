# Platform compatibility

Evidence: `CMakeLists.txt`, `cmake/*.cmake`, `CMakePresets.json`, `.github/workflows/*.yml`, `README.md`, `BUILDING.md`, and platform guards in `src/`.

## Established by CI (`Build.yml`)
- **Linux x86_64**: ubuntu-22.04, GCC 12, Ninja, zlib + LevelDB dev packages, C23/C++20.
- **Android ARM64**: NDK r26c, `android-24` (Android 7.0), `arm64-v8a`, static libc++ (`c++_static`), zlib and LevelDB cross-built. Termux native builds are documented in `BUILDING.md`.
- **Windows x86_64**: `windows-latest`, MSVC via `ilammy/msvc-dev-cmd`, vcpkg `zlib` and `leveldb` (`x64-windows-static-md`), and `CMAKE_C_STANDARD=17` because MSVC lacks full C23. `cmake/Windows.cmake` also has a MinGW branch (`-mthreads`).

## Claimed but not covered by CI
- **macOS**: named in `README.md`/`CONTRIBUTING.md` and guarded in code (`__APPLE__`, `-Wl,-export_dynamic`), but no CI job exists. Treat as unverified.

## Windows 10
The repository declares no minimum Windows OS version (no `_WIN32_WINNT`/`NTDDI` settings) and no document states Windows 10 support or testing. "Win10" appears only as the Minecraft **Windows 10 Edition client** (the `edition` byte in `LoginPacket`, where 0 = Pocket, and a recipe-UUID note in `CraftingManager.h`). Do not conflate the two:
- OS build: keep MSVC/MinGW paths compiling; avoid Win32 APIs newer than needed and keep them behind `_WIN32` guards. Do not claim Windows 10 OS support without a verified build.
- Client protocol: the server targets MCPE 0.15.x protocol 84 (Pocket edition). Do not assume other editions or protocol versions behave identically.

## Compatibility-sensitive code
- Threads: `utils/ThreadPort.c/.h` (pthreads vs Win32). Do not include pthread headers in code compiled by MSVC.
- File-system calls: `utils/PlatformCompat.h`. Sockets: `network/raklib/UDPServerSocket.c` (Winsock2 before `windows.h`; `WIN32_LEAN_AND_MEAN`, `NOMINMAX`).
- Crash backtraces: `CurseStone.cpp`/`CrashDump.cpp` pick `execinfo.h` on glibc/macOS and `_Unwind_Backtrace` on Android; `<execinfo.h>` is unavailable on older NDKs.
- Terminal/logging: `utils/Terminal.c`, `logger/AsyncMainLogger.c`.
- Build: `cmake/Android.cmake`, `cmake/Windows.cmake`, `cmake/FindLevelDB.cmake` (LevelDB is optional).
- Networking, protocol and world-format changes: read `CONTRIBUTING.md` section 5 (protocol 84 notes: block positions are signed `int32_t`; entity positions are `float` on the wire and `double` internally) and keep wire formats unchanged unless the task is to change them.

## Build-variant facts to respect
- Default CMake build is Release; CI Linux/Android use Release, `linux-debug` preset exists for Debug.
- C standard differs per target: C23 (Linux, Android, presets), C17 (Windows CI). A change that compiles on Linux can still break MSVC.
- `build_local.sh` is a GCC-only shortcut (`-std=c2x`/`-std=c++20`, `-w`, `-Isrc`, `-lz -ldl -rdynamic`). It has no include path or sources for `third_party/lua`, although `src/plugin` includes `<lua.hpp>`, so whether it still builds the current tree is **unverified**. It is not proof of CMake, Windows or Android compatibility.
- LevelDB is optional everywhere: code must compile with `CURSESTONE_LEVELDB=0`.
- `release.yml` packages zips for Linux x86_64, Android ARM64 and Windows x86_64, adding `start.sh` (Linux/Android) or `start.bat` (Windows); `Build.yml`'s tag release attaches the three binaries. Changing binary names or the working-directory layout affects these scripts and workflows.
- Working-directory contract: `server.properties`, `cursestone.yml`, `server.log`, `plugins/`, `data_pl/`, world folder are all relative to the launch directory (`Server::start(dataPath)`); keep that stable on all platforms.

## Rules
Preserve every platform above. Do not add platform restrictions, unsupported APIs or new dependencies that break an established target. State exactly which targets you built or could not build; never imply a Windows or Android build passed if only Linux was built.
