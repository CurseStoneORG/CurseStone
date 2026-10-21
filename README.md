<p align="center">
  <img src="logo.svg" alt="CurseStone logo" width="140">
  <br>
  <strong>Open-source server software for Minecraft: Pocket Edition 0.15.x</strong>
  <br>
  Written in C and C++20
</p>

<p align="center">
  <a href="https://github.com/CurseStoneTeam/CurseStone/actions/workflows/Build.yml">
    <img src="https://github.com/CurseStoneTeam/CurseStone/actions/workflows/Build.yml/badge.svg" alt="Build status">
  </a>
  <img src="https://img.shields.io/badge/license-LGPL--3.0--or--later-blue" alt="License: LGPL-3.0-or-later">
  <img src="https://img.shields.io/badge/MCPE%20protocol-84-8A2BE2" alt="MCPE protocol 84">
</p>

CurseStone is an open-source server implementation for Minecraft: Pocket Edition 0.15.x (protocol 84). Its C core handles low-level systems such as networking, NBT and chunk data, while the game logic is written in C++20.

The project is intended for people who want to run, study and modify their own server software. It is not designed to reproduce every feature of the original game.

## Features

- **Lua 5.4 plugins** — Lua is built into the server binary, so no separate runtime is required.
- **World storage** — Supports Anvil and LevelDB formats, with Anvil used by default.
- **Terrain generation** — Includes biome support, noise-based terrain and populators.
- **Runtime diagnostics** — Provides a runtime profiler through `/workbench` and crash dumps.
- **Multiple platforms** — Supports Linux, macOS, Windows and Android through Termux.

## Compatibility

CurseStone targets **Minecraft: Pocket Edition 0.15.x, protocol 84**.

It is not a modern Bedrock Edition server and does not implement every vanilla feature. Some gameplay systems, including mob AI and pathfinding, are not available.

Make sure your client uses a compatible game version before connecting.

## Building from source

You will need a C/C++ toolchain, CMake and Ninja. Refer to [BUILDING.md](BUILDING.md) for platform-specific dependencies, build presets, Android instructions and cross-compilation details.

For a standard Release build:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/CurseStone
