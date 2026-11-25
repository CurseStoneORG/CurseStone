<div align="center">

  <img src="logo.svg" alt="CurseStone" width="160">

  # CurseStone

  **A modern, native server for Minecraft: Pocket Edition**

  Open-source server software written in C++20.

  <p>
    <a href="https://github.com/CurseStoneORG/CurseStone/actions/workflows/Build.yml">
      <img src="https://github.com/CurseStoneORG/CurseStone/actions/workflows/Build.yml/badge.svg" alt="Build">
    </a>
    <img src="https://img.shields.io/badge/license-LGPL--3.0--or--later-blue" alt="License">
  </p>

  <p align="center">
    <a href="https://cursestoneorg.github.io/">Website</a> ·
    <a href="https://cursestoneorg.github.io/docs/">Docs</a> ·
    <a href="https://github.com/CurseStoneORG/CurseStone/releases">Releases</a> ·
    <a href="https://github.com/CurseStoneORG/CurseStone/issues">Issues</a>
  </p>

</div>

---

## Why CurseStone exists

CurseStoneORG builds a Minecraft: Pocket Edition server that treats the game with respect: one that
starts fast, runs as a single native program, behaves like the game players remember, and gives server
owners and plugin authors real control.

Pocket Edition has a loyal community that deserves server software that is actively maintained,
honest about what it does and doesn't do, and built to last. That is what we build here, in the open,
with everyone who wants to help.

## What CurseStone is

**CurseStone** is an independent server implementation for **Minecraft: Pocket Edition**.
The whole core is native C++20: networking, world generation, physics,
inventories, redstone, commands and the plugin system. It is not a fork and it does not need a
language runtime installed on your host.

## What you get

| | |
|---|---|
| **One native binary** | No interpreter or runtime to install. Download, run, play. Releases ship for **Linux x86-64**, **Linux ARM64**, **Android (Termux)** and **Windows x86-64**. |
| **Vanilla-faithful worlds** | A port of the vanilla Pocket Edition overworld generator: biome layout, rivers, mesa surfaces, vanilla structures, plus the vanilla liquid flow engine. Anvil and LevelDB world formats are supported. |
| **Two ways to write plugins** | Script in **Lua 5.4** (built in) or ship **native C++ plugins** (`.so` / `.dll`) for maximum speed. Both are documented and come with working examples. |
| **Client-translated messages** | Join/leave messages, command feedback, chat formats and death messages are rendered by the player's own game in their own language, with color support. |
| **Diagnostics you can read** | A compiler-style error console, runtime profiling, `/status` with tick-level costs, client-lost reports and crash memory dumps with symbolized backtraces. |
| **Built for hosting** | Linux builds are compiled against an old glibc ceiling (2.31) so they start on common hosting images, including Pterodactyl. Incomplete `server.properties` files are completed automatically, and the launcher script restarts the server after a crash. |
| **A real test client** | `cursestone-bot`, a protocol test client, exercises login, chunk streaming, containers and furnaces end to end. |

## Why choose CurseStone

We won't claim it is the best at everything. Here is where it genuinely stands out, and where it doesn't (yet).

**Choose CurseStone if you want:**

- **A single native program.** C++20 end to end, with per-tick work budgets, bounded chunk streaming and a main-thread-safe design written down in the open ([THREADING.md](docs/THREADING.md)). Less to install, less to break.
- **The classic game, not an approximation of it.** Mining rules, tool gating, liquids and terrain follow vanilla behavior, and every change is checked against a real game client.
- **Freedom in how you extend it.** Lua when you want to iterate quickly, native C++ when you need raw performance, with the same event model for both.
- **A project that explains itself.** Architecture notes, a published audit, per-version changelogs in English and Spanish, a documented plugin API and a Doxygen reference.
- **Hosting that just works.** Prebuilt binaries for four platforms, including Android and ARM64.
- **An open, welcoming codebase.** LGPL-licensed, with contribution guidelines and a style guide.

**Be aware that:**

- CurseStone supports specific client versions at a time. Check the [release notes](changelogs/README.md) for the versions each release supports.
- Not every vanilla mechanic is implemented. Mob AI and pathfinding, for example, are still in development.
- The project is in beta. Features move quickly, and the changelogs say exactly what changed.

If you want a stable, readable, hackable home for Pocket Edition multiplayer, this is it.

## Quick start

1. Download the archive for your platform from the [Releases](https://github.com/CurseStoneORG/CurseStone/releases) page.
2. Extract it and run `start.sh` (Linux / Termux) or `start.bat` (Windows).
3. Join with a supported Minecraft: Pocket Edition client (see the [release notes](changelogs/README.md)) using your server's address and port.

Prefer to build it yourself? See [docs/BUILDING.md](docs/BUILDING.md) (CMake + Ninja, GCC 12 or newer).

## Technology

| Technology | Purpose |
|---|---|
| C++20 | The whole server core: networking, game mechanics and low-level systems |
| Lua 5.4 | Integrated scripting for plugins |
| Native Plugin ABI | Function-pointer interface for `.so` / `.dll` plugins |
| CMake + Ninja | Build configuration and build system |

## Project status

CurseStone is actively developed. Systems and gameplay features keep evolving while compatibility
with Minecraft: Pocket Edition stays the priority. Support for more client versions is planned as the
project grows, without compromising the quality of the existing experience.

## Documentation

- **Building:** [docs/BUILDING.md](docs/BUILDING.md)
- **Plugin API (Lua + native C++):** [docs/PLUGINS.md](docs/PLUGINS.md)
- **Events:** [docs/EVENTS.md](docs/EVENTS.md)
- **Diagnostics:** [docs/DIAGNOSTIC_ENGINE.md](docs/DIAGNOSTIC_ENGINE.md) · [docs/CRASH_DIAGNOSTICS.md](docs/CRASH_DIAGNOSTICS.md)
- **Documentation index:** [docs/README.md](docs/README.md)
- **Release notes:** [changelogs](changelogs/README.md)
- **Online docs:** https://cursestoneorg.github.io/docs/
- **API reference (Doxygen):** https://cursestoneorg.github.io/doxygen-docs/

## Contributing

CurseStone grows with its community. Server code, plugins, testing, documentation, translations and bug
reports are all welcome. Start with [CONTRIBUTING.md](CONTRIBUTING.md) and use the
[issue tracker](https://github.com/CurseStoneORG/CurseStone/issues) to report bugs or suggest ideas.

## License

CurseStone is distributed under the **GNU Lesser General Public License, version 3.0 or later
(LGPL-3.0-or-later)**. Full text: <https://www.gnu.org/licenses/lgpl-3.0.html>.

---

<div align="center">

  **CurseStoneORG**

  *Developed and founded by @Dr1xyDev*

</div>
