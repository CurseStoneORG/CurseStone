
<div align="center">

  <img src="logo.svg" alt="CurseStone" width="160">

  # CurseStone

  **A new foundation for Minecraft Pocket Edition servers**

  Open-source server software developed in C++20

  <p>
    <a href="https://github.com/CurseStoneORG/CurseStone/actions/workflows/Build.yml">
      <img src="https://github.com/CurseStoneORG/CurseStone/actions/workflows/Build.yml/badge.svg" alt="Build">
    </a>
    <img src="https://img.shields.io/badge/license-LGPL--3.0--or--later-blue" alt="License">
    <img src="https://img.shields.io/badge/protocol-84-8A2BE2" alt="Protocol">
  </p>

  <p align="center">
    <a href="https://cursestoneorg.github.io/">Official Website</a>
    <br>
    <a href="https://github.com/CurseStoneORG/CurseStone">GitHub Repository</a>
    <br>
    <a href="https://github.com/CurseStoneORG/CurseStone/issues">Issues and Suggestions</a>
  </p>

</div>

---

## About the Project

**CurseStone** is an open-source server software project developed for **Minecraft: Pocket Edition**.

Built with C++20, it aims to provide an independent, extensible, and customizable server implementation for one of the most recognizable eras of Minecraft: Pocket Edition.

The project focuses on preserving compatibility with the original game while providing a foundation for server administrators and developers to create their own multiplayer experiences.

## Designed for Its Era

CurseStone is designed for the classic **Minecraft: Pocket Edition** environment.

It may be updated to support future versions at some point.

## Features

- **Plugin Support** — Extend server functionality through an integrated Lua 5.4 environment or native C++ plugins (.so/.dll).
- **World Generation** — Generates worlds similar to the vanilla **Minecraft: Pocket Edition** world generation, including vanilla structures and biomes.
- **World Management** — Supports Anvil and LevelDB world formats.
- **Developer Tools** — Runtime profiling, diagnostics, and crash memory dumps.
- **Cross-Platform** — Supports Linux, Windows, and Android.
- **Open Source** — Explore, modify, and contribute to the project's development.

## Technologies

| Technology | Purpose |
|---|---|
| C++20 | Complete server core: networking, game mechanics, and low-level systems |
| Lua 5.4 | Script-based plugin development (integrated interpreter) |
| Native Plugin ABI | Function-pointer-based interface for .so/.dll plugins |
| CMake | Build process configuration |
| Ninja | Build system |

## Project Status

CurseStone is an actively developed project. Its systems and gameplay features continue to evolve while maintaining compatibility with Minecraft: Pocket Edition.

The project aims to preserve the original game's behavior and world generation systems while gradually expanding server functionality.

Not all features from the original game have been implemented yet. In particular, some gameplay systems, such as mob AI and pathfinding, are not yet available.

## Documentation

- **Building:** [docs/BUILDING.md](docs/BUILDING.md)
- **Plugin API (Lua + Native C++):** [docs/PLUGINS.md](docs/PLUGINS.md)
- **Repository Documentation Index:** [docs/README.md](docs/README.md)
- **Release Notes:** [changelogs](changelogs/README.md)
- **Online Documentation:** https://cursestoneorg.github.io/docs/
- **API Reference (Doxygen):** https://cursestoneorg.github.io/doxygen-docs/

## Contributing

CurseStone is an open-source project, and contributions are welcome, whether you're interested in server development, plugin creation, testing, or improving existing systems.

Check out [CONTRIBUTING.md](CONTRIBUTING.md) to get started, and use the [issue tracker](https://github.com/CurseStoneORG/CurseStone/issues) to report bugs or suggest improvements.

## License

CurseStone is distributed under the **GNU Lesser General Public License, version 3.0 or later (LGPL-3.0-or-later)**.

See the [LICENSE](LICENSE) file for more information.

---

<div align="center">

  **CurseStoneORG**

  *Developed and founded by @Dr1xyDev*

</div>
