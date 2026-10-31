# Changes

## 2026-11-01 — Dead code out of the tree; chat encoded once and rate-limited

### Improved
- `Server::broadcastMessage`: the chat `TextPacket` is serialized once and the
  encoded `[pid][body]` bytes are reused for every player through the new
  `Player::sendEncodedBody()` (shared queue/flush path with `sendPacket()`),
  instead of re-encoding the same payload per recipient on the tick thread.
- `Player::handleChat`: chat anti-spam — at most one message every 500 ms per
  player (steady clock). Commands bypass the limit; extra messages are dropped
  silently before the `PlayerChatEvent` fires.

### Removed
- `raklib/protocol/` (65 files, ~3.75k lines): SessionManager.c / Session.c
  build every RakNet reply inline, so the packet-class wrappers were never
  called. The `MCP_RELIABILITY_*` constants both sides still use moved to
  `raklib/Reliability.h`.
- 11 gameplay packets nothing constructs or sends (AddItem, AddPainting,
  ClientboundMapItemData, HurtArmor, MapInfoRequest, PlayerInput,
  ReplaceSelectedItem, RiderJump, SetEntityLink, SpawnExperienceOrb,
  TelemetryEvent).
- 11 mob classes no spawner/egg/command instantiates plus the
  Animal/Monster/Creature bases left without subclasses.
- 59 headers unreachable from any translation unit (block defs, events that
  are never fired, ChunkLoader, SimpleChunkManager, EntityIds, ...), and the
  unused `ThreadManager`, `Timings`, `OfflinePlayer`, `AttributeMap` pairs.
- Empty `Server.cpp` stubs (`broadcastPacket`, `broadcastPacketsCallback`,
  whitelist mutators) and the unreferenced `resources/` samples.
- Total: 180 files, ~8.4k lines out of the tree and the binary.

### Reviewed, left as is
- `Diagnostics::recordPacket` and `WorkbenchManager::onPacketOut` still do one
  mutex lock + small string per packet, but both are bounded deques and
  Workbench is gated behind `active_`; they feed crash/disconnect dumps, so
  the cost is intentional.

### Tests
- Full Release rebuild clean after the removals and after the chat path
  refactor (Ninja, C++20).
- Runtime harnesses on the built binary, all passing:
  `furnace_natural_wire_check.py`, `chunk_explore_check.py` (340 sends /
  340 unique), `chunk_radius_reload_check.py`, `container_open_wire_check.py`.

## 2026-10-31 — Tick-thread cost: chat and /status latency

### Improved
- `BatchPacket::encode`: zlib level 7 -> 2 (runs on the tick thread per player each tick).
- `Player::onUpdate`: chunk spiral cached per player and radius; unsent chunks are found under one `chunks_mtx_` lock; the in-view set is built only when the player crosses a chunk border.
- `Diagnostics`: packet history is a `std::deque` (`pop_front`) instead of `vector::erase(begin())`.
- `ChunkManager::count()`: lock-free atomic mirror of `chunks_.size()`.
- `/status`: one packet per player instead of one per line, cached RSS read (2 s), player commands run directly on the main thread, 2 s cooldown per player.

### Fixed
- Chunks past an early `break` (byte budget) were left out of the in-view set and could be dropped and re-sent after a chunk-border move.

### Tests
- Full Release build (Ninja, C++20): clean, no new diagnostics on the touched files.
- Runtime harnesses on the built binary, all passing:
  `chunk_explore_check.py` (289/289 chunks, 340 sends / 340 unique — no
  re-sends after the byte-budget `break`), `chunk_radius_reload_check.py`
  (radius raise/lower, no re-send storm), `furnace_natural_wire_check.py`
  (natural vs placed furnace wire flow).

### Resolved (2026-11-01)
- `Server::broadcastMessage` now encodes once and reuses the bytes.
- `Player::handleChat` now has the 500 ms chat rate limit.
