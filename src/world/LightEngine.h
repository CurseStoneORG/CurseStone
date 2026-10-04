// SPDX-License-Identifier: LGPL-3.0-or-later
// LightEngine — sky and block light that actually behaves like vanilla.
//
// Before this, CurseStone had exactly one light pass
// (mcp_chunk_propagate_sky_light in the pure-C chunk layer) and it had
// three defects that match the reported symptoms one for one:
//
//   1. It only spread HORIZONTALLY. The BFS used the four neighbours
//      (+x, -x, +z, -z) and had no vertical step at all, so light could
//      never travel down through a cave mouth or under an overhang.
//      Every covered area stayed pitch black — the "black patches
//      under a ceiling" the player reported.
//
//   2. Block light was never computed at all. mcp_block_light_emission()
//      existed and was dead code, so a torch or a glowstone emitted
//      nothing into the chunk's block_light array and a cave lit only by
//      torches was black.
//
//   3. Nothing ever updated the light after a block changed.
//      World::setBlock() wrote the block and stopped, so building a roof
//      left the room under it at full daylight, and breaking it left the
//      room in the dark.
//
// This engine fixes all three: it floods in the six vanilla directions,
// seeds both layers, and recomputes a bounded region around every change
// (and around every newly loaded chunk, so chunk borders have no seam).
//
// Algorithm, per layer: clear the region, seed it (open sky from the
// heightmap / light emitters), seed it again from the light just OUTSIDE
// the region so nothing that flows in from the rest of the world is lost,
// then BFS outwards with a -1-per-step cost (more through opaque blocks).
// Clearing and re-flooding is what makes darkening work too: a roof going
// up simply lowers the seed value underneath it.
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace cursestone::world {

class World;

class LightEngine {
public:
    // Light falls off after 15 steps, so a region 15 blocks wider than the
    // change on every side can never miss a source further away: the
    // recomputation is self-contained.
    static constexpr int32_t kRelightRadius = 15;
    // A chunk that just appeared needs its neighbours relit too, so the
    // sweep extends one full light range past the chunk border.
    static constexpr int32_t kChunkRelightRadius = 15;

    // Chunks relit per server tick, so loading a view full of chunks
    // never stalls the main loop in one giant burst.
    static constexpr int32_t kPendingBudget = 2;

    explicit LightEngine(World& lvl) : lvl_(lvl) {}

    // Recompute both layers in a cube around a block change.
    void relightAround(int32_t x, int32_t y, int32_t z, int32_t radius);

    // Same sweep, centred on a chunk, used when a chunk is generated or
    // loaded so its border with the already-lit neighbours is continuous.
    void relightChunk(int32_t cx, int32_t cz, int32_t radius);

    // Called from the chunk worker thread when a chunk enters the
    // manager. Only queues the chunk — doing real work here would block
    // generation and risk lock inversion with ChunkManager::mtx_.
    void onChunkAdded(int32_t cx, int32_t cz);

    // Drain up to `budget` queued chunks. Returns how many were done.
    size_t processPending(int32_t budget);

    bool hasPending() const;

private:
    struct Cell {
        int32_t x, y, z;
    };

    World& lvl_;
    mutable std::mutex mtx_;
    std::deque<std::pair<int32_t, int32_t>> pending_;

    // Runs one full region recomputation for both layers.
    void relight(int32_t x0, int32_t y0, int32_t z0,
                 int32_t x1, int32_t y1, int32_t z1);
};

} // namespace cursestone::world