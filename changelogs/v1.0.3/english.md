# CurseStone v1.0.3 Beta Release

Changes since Beta v1.0.2.

### Added
- Ported the vanilla MCPE 1.0.0 overworld generator and removed the old generator: sturdy biome layout, natural terrain relief, vanilla plant survival rules, and the extreme-hills grass tint.
- Vanilla-style rivers that carve their own beds and valleys, including the highland river drop, and mesa surfaces painted with hardened clay and colored bands.
- Vanilla double chests with 54-slot container windows, synchronized co-viewers, and per-half item drops when broken.
- The landing dust and sprint particle effects that the 0.15 client never rendered.
- Complete client-facing entity metadata and experience (XP) synchronization.
- Linux ARM64 (Cobalt 100) build support and release packaging, alongside the existing Linux x86-64, Android ARM64, and Windows x86-64 archives.
- A configurable server list name in `cursestone.yml`.
- A client-lost report written when a session dies without a clean disconnect, easing network diagnostics.
- The per-tick light drain cost shown in the `/status` diagnostics output.
- `cursestone-bot`, a real-shaped protocol-84 test client used to exercise login, chunk streaming, containers, and furnace interaction end-to-end, including a scenario where the bot taps a natural village furnace.
- A RakLib session use-after-free investigation report and a native-C++ architecture audit document.
- A new project logo.

### Changed
- Disabled furnace generation in the vanilla world generator while a crash triggered by touching a naturally generated furnace is investigated.
- Reworked world generation polish: wide vanilla rivers, consistent surface-depth noise sampled in world block coordinates so terrain matches across chunk borders, and each populate step now works on a private 3x3 neighborhood copy.
- Ported the vanilla 1.0.0 liquid flow engine: generated water and lava are shipped settled, saved still liquids wake correctly when their chunk loads, and update storms near water are gone.
- Streamed chunks smoothly with bounded per-tick work, and re-sent chunks the client drops when its render radius changes.
- Updated the generator heightmap incrementally on block writes instead of rebuilding it.
- Made mining vanilla: drops are gated by the correct tool and break speed is verified server-side.
- Migrated the first-party C layer to native C++: NBT is now a native class API with zero C linkage, RAII handles guard C-boundary buffers and NBT tags, the light engine queries the Block registry directly, and RakLib sessions are indexed in O(1).
- Made chunk-storm light relights 3x cheaper with identical results, cut per-populate allocations and structure rescans, reduced tick-thread costs in chunk streaming and batching, encoded chat once per broadcast with a chat rate limit, and queued congestion-refused payloads.
- Furnace progress is now streamed only while the fire burns, and tile NBT re-sends were dropped from the container-open batch.
- Gated all console debug output behind debug-mode.
- Shipped debug-symbol binaries with symbolized crash backtraces and removed always-on session dumps.
- Hardened memory lifecycle, allocation bounds, and shutdown ordering.
- Updated the server and plugin API version through `CURSESTONE_API`, set to `1.0.3`.
- Normalized every file header to the CurseStone banner, translated comments and log lines to English, revised the README, and removed 152 never-included headers, dead transport packets, unspawned mobs, orphaned events, and stale stubs.

### Fixed
- Fixed incomplete `server.properties` files being used as-is: a file that exists but is empty or contains only a couple of options (for example, the short defaults shipped by hosting panels such as Pterodactyl) is now completed with every missing option instead of running as-is.
- Fixed a RakLib session use-after-free with a single owner-side teardown, and an inverted TagGuard ownership that double-freed saved NBT lists.
- Fixed furnace interaction for the 0.15 client: furnaces open as generic container windows, the furnace opens before held-item handling, and the full furnace spawn compound is shipped.
- Fixed chest lids animating with the era-correct block event.
- Fixed plugin-cancelled block edits desyncing the client: the client is now told the truth when a plugin cancels its block edit, and empty read-backs in Lua event hooks are guarded.
- Fixed the burning overlay not appearing on clients.
- Fixed fire extinguishing only for submerged faces; water now puts fire out on any touch.
- Fixed time synchronization by pinning the world clock where the client actually binds it.
- Fixed the player list keeping entries of players who already quit.
- Fixed the container open wire flow to match the protocol-84 reference, honored container close window ids, and broadcast slot changes to every co-viewer.
- Fixed chunk re-streams losing tile actors: tiles are rescanned on every stream so re-sent blocks keep their chests, furnaces, and signs.
- Fixed a section nibble index desync that corrupted block data.
- Fixed biome layer races, trees cut at chunk borders, and wrong log data.
- Fixed blocks being placeable inside entity hitboxes.
- Fixed bucket pours and fills on the 0.15 bucket wire form.
- Fixed container drops spilling in every game mode and removed the drop-magnet jitter.
- Fixed snow layer behavior when waking generated liquids.
- Fixed creative combat: attack knockback, the victim self-view hurt flash, the block-tap bow pose, phantom arrow flashes near players, and hotbar relinks so creative equipment takes effect correctly.
- Fixed the Windows console reader stdin wait loop and made process exit no longer block on the console reader.
- Fixed the Windows build with MSVC: C99 compound literals in Chunk.cpp and a BlockID equality ambiguity flagged by clang and MSVC.
- Fixed CI build triggers and dropped the optional LevelDB dependency.
- Fixed the missing generator include path in `build_local.sh`.

### Notes
- Compared against Beta v1.0.2.
- Furnace generation in the vanilla world generator is disabled because a crash when touching a naturally generated furnace is being investigated.
- Generated structures currently do not contain chests; chests will be added in a future update.
- This update does not include major plugin API changes.
- Target client: Minecraft: Pocket Edition 0.15.x.
- Network protocol: 84.
- CurseStone is server software and does not implement every vanilla Minecraft mechanic.
- Some features may require additional in-game testing to verify complete client compatibility.
