<p align="center">
  <img src="logo.svg" alt="The CurseStone logo" width="140">
  <br>
  <b>A lightweight, open source server software for Minecraft: Pocket Edition 0.15.x, written in C and C++</b>
</p>

<p align="center">
  <a href="https://github.com/CurseStoneTeam/CurseStone/actions/workflows/Build.yml"><img src="https://github.com/CurseStoneTeam/CurseStone/actions/workflows/Build.yml/badge.svg" alt="Build" /></a>
  <img alt="License" src="https://img.shields.io/badge/license-LGPL--3.0--or--later-blue">
  <img alt="Protocol" src="https://img.shields.io/badge/MCPE%20protocol-84-8A2BE2">
</p>

## What is this?
CurseStone is a server software for Minecraft: Pocket Edition 0.15.x (protocol 84). It has a plain C core for networking, NBT and chunk data, with the game logic written in C++20.

If you want to run a small MCPE server that you can read, build and modify, this is for you.

- 🧩 **Lua plugins** - Lua 5.4 is built into the binary, nothing extra to install
- 🗺️ **Two world formats** - `anvil` (default) and `leveldb`
- 🌍 **Terrain generation** - biomes, noise and populators included
- 🔧 **Built-in diagnostics** - a runtime profiler (`/workbench`) and crash dumps
- 📱 **Runs almost anywhere** - Linux, macOS, Windows and Android (Termux)

## :x: CurseStone is NOT a vanilla server.
It targets an old protocol on purpose and does not implement everything from the vanilla game. For example, entities have no pathfinding or mob AI.

If you need the full vanilla experience, use the official server software instead.

## Getting Started
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/CurseStone
```

On first run, a setup wizard creates the configuration and a fresh world. The server listens on **UDP 19132** by default, so open that port in your firewall.

- [Building from source](BUILDING.md) - requirements, presets, Android and cross-compiling
- [Prebuilt binaries](https://github.com/CurseStoneTeam/CurseStone/actions) - Linux x86_64 and Android ARM64 builds are attached to each workflow run as artifacts
- [Plugin documentation](https://github.com/CurseStoneTeam/cursestone-docs) - API reference and a ready-to-run example

## Configuration
CurseStone reads `server.properties` and `cursestone.yml` from the working directory. Both are created with built-in defaults on first run. `server.properties` holds the basic settings; `cursestone.yml` holds the advanced ones. Plugin data is stored in `data_pl/` at the server root.

The most common settings are `server-port`, `max-players`, `view-distance`, `gamemode`, `difficulty`, `world-format` and `autosave-ticks`. The auto-generated files document every option.

## Developing Plugins
Plugins are written in Lua. The documentation, API reference and an example plugin live in [cursestone docs by Doxygen](https://CurseStoneTeam.github.com).

## Contributing to CurseStone
Contributions are welcome. The following resources will help you get started.
 * [Building from source](BUILDING.md)
 * [Contributing Guidelines](CONTRIBUTING.md) - the C/C++ boundary rules, naming, protocol notes and pull request workflow
 * [Comment style guide](docs/STYLE_COMMENTS.md)

For anything larger than a small fix, please open an issue first so the approach can be agreed.

## AI development tools
Instructions and documentation for AI coding assistants (Claude Code) live in the [`.claude/`](.claude/) folder. Start with [`.claude/README.md`](.claude/README.md).

## Licensing information
This project is licensed under LGPL-3.0-or-later. Please see the [license text](https://www.gnu.org/licenses/lgpl-3.0.html) for details.

CurseStone is not affiliated with Mojang. All brands and trademarks belong to their respective owners. CurseStone is not a Mojang-approved software, nor is it associated with Mojang.
