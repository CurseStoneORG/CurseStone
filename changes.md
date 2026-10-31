# Changes

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

### Pending
- `Server::broadcastMessage` still serializes one `TextPacket` per player.
- No chat rate limit in `Player::handleChat`.
