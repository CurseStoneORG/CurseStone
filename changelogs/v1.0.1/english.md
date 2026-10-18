# CurseStone v1.0.1 Beta Release

### Added
- Added a Lua 5.4 plugin system with manifests, runtime bindings, built-in YAML and JSON parsers, and the `/plugins` command.
- Added plugin permission nodes, dynamic `/help` generation, and default `cursestone.*` permissions for operators.
- Added new biomes, vanilla-style terrain noise, cave generation, lakes, multiple tree types, villages, and desert temples.
- Added hunger, health synchronization, environmental damage, and player sneak and sprint animations.
- Added respawn handling, default skins for clients without skins, torch and lava lighting, and lit and unlit furnace states.
- Added persistent chest contents and block entities for chests and furnaces in generated structures.
- Added shared item-to-NBT conversion, swept-AABB movement physics, ray tracing, and improved crash diagnostics.
- Added packet-error reports, standalone build support through `build_local.sh`, development checks, benchmarks, and source license headers.

### Changed
- Renamed the Level API to World throughout the codebase, including world events, configuration keys, and source directories.
- Updated the server and plugin API version to `1.0.1`.
- Split player functionality into dedicated source modules.
- Moved crafting recipes and creative inventory registration into code instead of JSON resources.
- Improved server tick scheduling, Nether terrain generation, and chunk streaming limits.
- Lowered the default RakNet MTU to 1400.
- Rewrote the README and CONTRIBUTING documentation.

### Fixed
- Fixed multiple RakNet reliability issues involving duplicate messages, connection handshakes, login, retransmissions, and timeouts.
- Improved connection stability so one stalled or idle player is less likely to affect other players.
- Fixed Linux login packet parsing, item pickup packet IDs, block update encoding, player skin synchronization, and sprint action handling.
- Fixed crashes caused by signs with empty lines and out-of-bounds sign text writes.
- Reduced main-thread stalls during autosaving.

### Removed
- Removed obsolete recipe, creative-item, and biome JSON resources.
- Removed the old Level source directory, SimplexNoise implementation, and legacy `main.cpp` and `main.h` files.
- Removed the hard-coded command list previously used by `/help`.