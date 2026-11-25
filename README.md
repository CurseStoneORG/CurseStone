<div align="center">

  <img src="logo.svg" alt="CurseStone" width="160">

  # CurseStone

  **A modern, native server for Minecraft: Pocket Edition**

  Fast to start. Faithful to the game. Yours to customize.

  <p>
    <img src="https://img.shields.io/badge/license-LGPL--3.0--or--later-blue" alt="License">
  </p>

  <p align="center">
    <a href="https://cursestoneorg.github.io/">Website</a> ·
    <a href="https://cursestoneorg.github.io/docs/">Docs</a> ·
    <a href="https://github.com/CurseStoneORG/CurseStone/releases">Download</a> ·
    <a href="https://github.com/CurseStoneORG/CurseStone/issues">Issues</a>
  </p>

</div>

---

## What is CurseStone?

**CurseStone** is server software for **Minecraft: Pocket Edition**. It lets you host your own
multiplayer world for you and your friends, or for a whole community, with a server that starts
quickly, plays like the game people remember, and gives you real control over how it works.

It is a single native program: no extra runtime to install and nothing to set up before you play.

## What you get

### A server that just works
- **Download and run.** Ready-made builds for **Linux (x86-64 and ARM64)**, **Android (Termux)** and **Windows**.
- **Made for hosting.** Runs on common hosting images and panels, including Pterodactyl.
- **Forgiving setup.** A short or incomplete `server.properties` is completed with the missing options for you.
- **Self-healing.** The included launcher restarts the server automatically if it crashes.
- **Your own server name** in the server list, set in `cursestone.yml`.

### The game, as players remember it
- **Vanilla-style worlds.** Natural biomes, rivers, mesa terrain, villages and the vanilla way water and lava flow.
- **Vanilla gameplay rules.** Mining needs the right tool and follows real break speeds, chests (including double chests), furnaces, buckets, fire, combat, redstone and more.
- **Your worlds, your format.** Works with Anvil and LevelDB worlds.
- **Messages in every player's language.** Join and leave messages, command feedback, chat and death messages are shown by each player's own game, in their own language, with colors.

### Make it your own
- **Plugins in Lua.** Write scripts with the built-in Lua 5.4 support, no compiling needed.
- **Native plugins for speed.** Ship `.so` / `.dll` plugins when you need maximum performance.
- **Events and commands.** Hook into player actions, cancel or change them, and add your own commands and permissions.
- **Examples included**, such as spawn protection and floating text.

### Clear answers when something goes wrong
- **Readable errors** that say what failed, where and how to fix it, instead of walls of log text.
- **`/status`** shows how the server is performing, including per-tick costs.
- **Crash reports** with readable backtraces, so problems can be fixed instead of guessed at.

## Why choose CurseStone

- **Light and fast to run.** One native program written in C++20, with bounded per-tick work so busy moments don't freeze your server.
- **Faithful to the game.** Worlds and mechanics follow vanilla behavior instead of approximating it.
- **Flexible.** Lua for quick ideas, native code for demanding plugins, the same events for both.
- **Runs almost anywhere.** From a Windows PC to a Linux host or an Android phone.
- **Open and maintained.** Free software under the LGPL, developed in the open and updated with clear release notes.

### Honest limits
- Each release supports specific client versions. Check the [release notes](changelogs/README.md) to see which.
- Not every vanilla mechanic is implemented yet. Mob AI and pathfinding, for example, are still in development.
- The project is in beta and moves quickly.

## Quick start

1. Download the archive for your platform from the [Releases](https://github.com/CurseStoneORG/CurseStone/releases) page.
2. Extract it and run `start.sh` (Linux / Termux) or `start.bat` (Windows).
3. Join with a supported Minecraft: Pocket Edition client using your server's address and port.

## Learn more

- **Plugins (Lua + native C++):** [docs/PLUGINS.md](docs/PLUGINS.md)
- **Release notes:** [changelogs](changelogs/README.md)
- **Online documentation:** https://cursestoneorg.github.io/docs/
- **Website:** https://cursestoneorg.github.io/

## Community and contributing

CurseStone grows with its community. Plugins, bug reports, testing, translations and code are all
welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) to get started, and use the
[issue tracker](https://github.com/CurseStoneORG/CurseStone/issues) to report bugs or suggest ideas.

## License

CurseStone is distributed under the **GNU Lesser General Public License, version 3.0 or later
(LGPL-3.0-or-later)**. Full text: <https://www.gnu.org/licenses/lgpl-3.0.html>.

---

<div align="center">

  **CurseStoneORG**

  *Developed and founded by @Dr1xyDev*

</div>
