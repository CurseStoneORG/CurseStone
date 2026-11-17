# Platform compatibility

Evidence: `CMakeLists.txt`, `cmake/*.cmake`, `CMakePresets.json`, `.github/workflows/*.yml`, `README.md`, `docs/BUILDING.md`, and platform guards in `src/`.

## Established by CI (`Build.yml`, four build jobs)
- **Linux x86_64**: ubuntu, GCC, Ninja, zlib + LevelDB dev packages, C++20.
- **Linux ARM64**: cross-compiled on ubuntu with `gcc-aarch64-linux-gnu`, zlib cross-built (targets Cobalt 100-class ARM64 hardware).
- **Android ARM64**: NDK r26c, `android-24` (Android 7.0), `arm64-v8a`, static libc++ (`c++_static`), zlib and LevelDB cross-built. Termux native builds are documented in `docs/BUILDING.md`.
- **Windows x86_64**: `windows-latest`, MSVC via `ilammy/msvc-dev-cmd`, vcpkg `zlib` and `leveldb` (`x64-windows-static-md`). `cmake/Windows.cmake` also has a MinGW branch (`-mthreads`). The Windows job also uploads the `.pdb` for symbolized crash dumps.
- A tag release job (`Create Release`, on `v*` tags) attaches the four binaries.

## Claimed but not covered by CI
- **macOS**: named in `README.md`/`CONTRIBUTING.md` and guarded in code (`__APPLE__`, `-Wl,-export_dynamic`), but no CI job exists. Treat as unverified.

## Windows
The repository declares no minimum Windows OS version (no `_WIN32_WINNT`/`NTDDI` settings). "Win10" appears only as the Minecraft **Windows 10 Edition client** (the `edition` byte in `LoginPacket`, where 0 = Pocket, and a recipe-UUID note in `CraftingManager.h`). Do not conflate the two:
- OS build: keep MSVC/MinGW paths compiling; avoid Win32 APIs newer than needed and keep them behind `_WIN32` guards. Do not claim Windows 10 OS support without a verified build.
- Client protocol: the server targets MCPE 0.15.x protocol 84 (Pocket edition). Do not assume other editions or protocol versions behave identically.

## Compatibility-sensitive code
- Threads: `utils/ThreadPort.cpp/.h` (pthreads vs Win32). Do not include pthread headers in code compiled by MSVC.
- File-system calls: `utils/PlatformCompat.h`. Sockets: `network/raklib/UDPServerSocket.cpp` (Winsock2 before `windows.h`; `WIN32_LEAN_AND_MEAN`, `NOMINMAX`).
- Crash backtraces: `CurseStone.cpp`/`CrashDump.cpp` pick `execinfo.h` on glibc/macOS and `_Unwind_Backtrace` on Android; `<execinfo.h>` is unavailable on older NDKs.
- Terminal/logging: `utils/Terminal.cpp`, `logger/AsyncMainLogger.cpp`.
- Build: `cmake/Android.cmake`, `cmake/Windows.cmake`, `cmake/FindLevelDB.cmake` (LevelDB is optional).
- Networking, protocol and world-format changes: read `CONTRIBUTING.md` "Protocol 84 notes" (block positions are signed `int32_t`; entity positions are `float` on the wire and `double` internally) and keep wire formats unchanged unless the task is to change them.

## Build-variant facts to respect
- Default CMake build is Release; CI uses Release, the `linux-debug` preset exists for Debug.
- A change that compiles on Linux can still break MSVC or the NDK; platform-specific sources live in `cmake/`.
- `build_local.sh` is a GCC/Clang-only shortcut (`-std=c++20 -O2 -g -w -Isrc -Ithird_party/lua`, links `-lpthread -lz -ldl -rdynamic`). It passes `-w`, so it hides warnings; never use it to claim "no new warnings". It is not proof of CMake, Windows or Android compatibility.
- LevelDB is optional everywhere: code must compile with `CURSESTONE_LEVELDB=0`.
- `release.yml` packages zips for Linux x86_64, Linux ARM64, Android ARM64 and Windows x86_64, adding `start.sh` (Linux/Android) or `start.bat` (Windows); `Build.yml`'s tag release attaches the four binaries. Changing binary names or the working-directory layout affects these scripts and workflows.
- Working-directory contract: `server.properties`, `cursestone.yml`, `server.log`, `plugins/`, `data_pl/`, world folder are all relative to the launch directory (`Server::start(dataPath)`); keep that stable on all platforms.

## Rules
Preserve every platform above. Do not add platform restrictions, unsupported APIs or new dependencies that break an established target. State exactly which targets you built or could not build; never imply a Windows or Android build passed if only Linux was built.
