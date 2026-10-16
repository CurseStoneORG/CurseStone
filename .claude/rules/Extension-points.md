# Where and how to add things (each step verified in the code)

Read the neighboring implementation first and copy its exact shape. Always add the Doxygen and header required by `Rules.md`.

| Adding | Do this |
|---|---|
| Block | New `src/block/Block<Name>.h` (+`.cpp` if logic), derived from `Block`/an existing base (`BlockFallable`, ...). Add its ID to `block/BlockIds.h` if new. Register it in `block/BlockRegistry.cpp` (`Block::registerBlock(std::make_unique<BlockX>())`): an unregistered block does nothing. Names follow `Block` + Adjective + Noun. |
| Item | New `src/item/Item<Name>.h/.cpp`; register in `item/ItemRegistry.cpp` (`Item::registerAll`, `registerItem(...)`). Names: `Item` + Material + Noun in PascalCase. |
| Block entity | `src/tile/Tile<Name>`; add the block ID mapping in `tile/TileFactory.cpp` (`createTileForBlock` and its "can build" predicate). Check how `Spawnable`/`Container` are used by `TileChest`/`TileFurnace`. |
| Entity | `src/entity/Entity<Name>`, derived from the closest of `Animal`, `Monster`, `Creature`, `Living`, `Projectile`; add its ID in `entity/EntityIds.h`; see how an existing spawn path (grep the sibling class name) wires it. |
| Packet | `src/network/protocol/<Name>Packet.h/.cpp` derived from `DataPacket`; add the ID constant in `protocol/Info.h` and a `case` in `PacketPool::create` (`PacketPool.cpp`); handle it in `Player::handleDataPacket` (`player/PlayerNetwork.cpp`). Only protocol 84 layouts; block positions are signed `int32_t`. |
| Built-in command | `src/command/defaults/<Name>Command.h/.cpp`, then register in `command/Defaults.cpp`; check permissions via `permission/`. |
| Event | `src/event/<domain>/<Name>Event.h`; fire with `EventManager::get().call(ev)` and honor cancellation as neighboring call sites in `player/` do. Exposing it to Lua also needs a mapping in `plugin/BindingsEvents.cpp`. |
| Lua API function | The matching `plugin/Bindings*.cpp` (`BindingsCore`, `BindingsCommands`, `BindingsData`, `BindingsEvents`, `BindingsWorld`). Validate every argument and every path (`PathSafe.h`); it is a public API change, so note it in docs and `changes.md`. |
| World generator or populator | `src/world/generator/` (and subfolders); register by name where `World.cpp` selects `generator` strings. Verify with `tools/gen_smoke.cpp`/`gen_perf.cpp` style checks. |
| World storage format | `src/world/format/<format>/` implementing `WorldProvider`; register in `WorldProviderManager`. |
| Config key | Read it in `Server::loadConfig` (or the owning subsystem) with a default, add it to the right file in `utils/DefaultConfig.cpp` (`server.properties` for basic, `cursestone.yml` for advanced), update README config notes, flag in `changes.md`. |
| Standalone check or benchmark | `tools/`; follow `Testing.md`. |

New `.c`/`.cpp` files are picked up by the CMake glob; no build-file edit is needed. Cross-check `CONTRIBUTING.md` section 6 if this table seems to disagree.
