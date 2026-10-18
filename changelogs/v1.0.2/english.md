# CurseStone v1.0.2 Beta Release

Changes since Beta v1.0.1.5.

### Added
- Added Windows x86-64 build support, MSVC compatibility, Windows startup scripts, and platform-specific release archives.
- Added Nether portals, portal linking, coordinate conversion, and travel between the Overworld and Nether.
- Added multiworld management, independent world ticking, world loading and unloading, and autosaving.
- Added Nether terrain generation improvements, including bedrock boundaries, glowstone clusters, and fire placement.
- Added vanilla-style beds with sleeping animations, spawn points, player synchronization, and nighttime skipping when all players in a world are asleep.
- Added hunger and food mechanics, including saturation, exhaustion, regeneration, starvation, and persistent player data.
- Added food items, including baked potatoes, beetroot, beetroot soup, fish, mutton, rabbit, rabbit stew, cookies, pumpkin pie, golden carrots, enchanted golden apples, rotten flesh, and spider eyes.
- Added drinkable potions, splash potions, projectile collisions, shatter effects, and area-based effect application.
- Added 23 status effects: Speed, Slowness, Haste, Mining Fatigue, Strength, Instant Health, Instant Damage, Jump Boost, Nausea, Regeneration, Resistance, Fire Resistance, Water Breathing, Invisibility, Blindness, Night Vision, Hunger, Weakness, Poison, Wither, Health Boost, Absorption, and Saturation.
- Added milk buckets that remove active status effects and leave an empty bucket.
- Added bow drawing and release mechanics, arrow flight, projectile collisions, arrow pickup, and despawning.
- Added falling-block physics for sand, gravel, and anvils.
- Added custom item names and improved NBT preservation across inventories, equipment, containers, and dropped items.
- Expanded the Lua plugin API with world management, player manipulation, resource access, and additional player, entity, bed, door, and interaction events.
- Added Lua command overrides with automatic restoration of built-in commands when plugins unload.
- Added Lua execution limits and automatic plugin disabling after repeated errors or excessive execution time.
- Added `/kill <player>` and the operator-only `/status` command with TPS, memory, uptime, player count, plugin count, and world diagnostics.
- Added a first-run setup wizard, embedded default configuration templates, and startup scripts for Linux, Termux, and Windows.
- Added Doxygen documentation generation, automated publishing, development guidelines, and code-formatting rules.

### Changed
- Separated basic settings in `server.properties` from advanced settings in `cursestone.yml`.
- Moved plugin data storage to the server-root `data_pl/` directory.
- Updated the server and plugin API version to `1.0.2`.
- Improved damage calculations for environmental damage, armor, creative mode, resistance, strength, weakness, and absorption.
- Improved multiplayer synchronization for health, hunger, food consumption, equipment, item use, bows, arrows, knockback, death, respawning, and item pickup.
- Improved player data persistence for health, hunger, saturation, exhaustion, active effects, and bed spawn positions.
- Optimized incremental chunk autosaving across loaded worlds.
- Improved item-stack validation, maximum stack sizes, and NBT-aware item merging.
- Improved Windows portability and linked zlib and LevelDB statically to avoid missing runtime DLL dependencies.
- Standardized source formatting and comments and removed obsolete source stubs and outdated test utilities.

### Fixed
- Fixed Nether portal detection, ignition, metadata, linking, and dimension travel.
- Fixed beds and doors becoming ghost blocks or failing to synchronize correctly.
- Fixed bed sleeping animations and sleep data synchronization.
- Fixed chunk streaming and login packet ordering to improve initial chunk delivery.
- Fixed falling-block network entity IDs and client rendering.
- Fixed fire interactions, environmental damage, and fall damage.
- Fixed potion rendering, splash shatter effects, and status-effect synchronization on MCPE 0.15.10.
- Fixed multiplayer food consumption, milk usage, health updates, and hunger synchronization.
- Fixed multiplayer bow animations, arrow spawning, projectile movement, and release behavior.
- Fixed dropped items remaining visible after pickup and improved item synchronization between players.
- Fixed item duplication, invalid stack sizes, and incorrect merging of items with different NBT data.
- Fixed custom item names and NBT data being lost during inventory, container, equipment, and death-drop operations.
- Fixed player death and respawn handling and chest block-event packet handling.
- Fixed plugin event bindings and message handling.
- Fixed operator-list parsing on Android, including UTF-16 files and inconsistent entry formatting.
- Fixed Windows compilation, threading, POSIX compatibility, and release packaging issues.
- Fixed startup configuration defaults and behavior when configuration files are missing.

### Notes
- Target client: Minecraft: Pocket Edition 0.15.x.
- Network protocol: 84.
- CurseStone is server software and does not implement every vanilla Minecraft mechanic.
- Some features may require additional in-game testing to verify complete client compatibility.