<div align="center">

  <img src="logo.svg" alt="CurseStone" width="160">

  # CurseStone

  **A new foundation for Minecraft: Pocket Edition 0.15.10 servers.**

  Open-source server software built with C++20.

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
    <a href="https://github.com/CurseStoneORG/CurseStone/issues">Issues</a>
  </p>

</div>

---

## About

**CurseStone** is an open-source server software project built specifically for **Minecraft: Pocket Edition 0.15.10**.

Developed in C++20, it aims to provide an independent, extensible and customizable server implementation for one of the most recognizable eras of Minecraft: Pocket Edition.

The project focuses on preserving compatibility with the original game while providing a foundation for server owners and developers to build their own multiplayer experiences.

## Built for its era

CurseStone targets the legacy Minecraft: Pocket Edition environment, using **protocol 84**.

Rather than following modern Bedrock Edition development, the project remains focused on its original target version, maintaining a clear direction for its architecture and gameplay implementation.

## What it offers

- **Plugin Support** — Extend server functionality using an integrated Lua 5.4 runtime.
- **Vanilla World Generation** — Generate worlds using terrain generation systems designed to reproduce the original Minecraft: Pocket Edition 0.15.10 world generation.
- **World Management** — Anvil and LevelDB world storage support.
- **Terrain Generation** — Biomes, procedural terrain, noise-based generation and world populators.
- **Developer Tools** — Runtime profiling, diagnostics and crash dumps.
- **Cross-Platform** — Support for Linux, Windows, macOS and Android through Termux.
- **Open Source** — Explore, modify and contribute to the project's development.

## Technology

| Technology | Purpose |
|---|---|
| C++20 | Entire server core: networking, gameplay and low-level systems |
| Lua 5.4 | Plugin scripting |
| CMake | Build configuration |
| Ninja | Build system |

## Project Status

CurseStone is an actively developed project. Its systems and gameplay features continue to evolve, with compatibility for Minecraft: Pocket Edition 0.15.10 remaining the primary target.

The project aims to preserve the original game's behavior and world generation while progressively expanding its server functionality.

Not every original game feature is currently implemented. In particular, some gameplay systems, including mob AI and pathfinding, are not yet available.

## Documentation

- **Building:** [docs/BUILDING.md](docs/BUILDING.md)
- **Repository docs index:** [docs/README.md](docs/README.md)
- **Release notes:** [changelogs](changelogs/README.md)
- **Online documentation:** https://cursestoneorg.github.io/docs/
- **API reference (Doxygen):** https://cursestoneorg.github.io/doxygen-docs/

## Contributing

CurseStone is open source and contributions are welcome, whether you are interested in server development, plugin creation, testing or improving existing systems.

Read [CONTRIBUTING.md](CONTRIBUTING.md) to get started, and use the [issue tracker](https://github.com/CurseStoneORG/CurseStone/issues) to report problems.

## License

CurseStone is licensed under the **GNU Lesser General Public License v3.0 or later (LGPL-3.0-or-later)**.

See the [LICENSE](LICENSE) file for details.

---

<div align="center">

  **CurseStoneORG**

  *Built for Minecraft: Pocket Edition 0.15.10.*

</div>
