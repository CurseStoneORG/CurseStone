// SPDX-License-Identifier: LGPL-3.0-or-later
#include "LightEngine.h"

#include "ChunkManager.h"
#include "World.h"
#include "block/BlockLightC.h"
#include "world/format/Chunk.h"

#include <algorithm>
#include <deque>
#include <vector>

namespace cursestone::world {

namespace {

constexpr int32_t kWorldHeight = 128;

// Light is stored as a nibble and tops out at 15, so 15 blocks is the
// furthest anything can travel.
constexpr uint8_t kMaxLight = 15;

const int kDX[6] = {1, -1, 0, 0, 0, 0};
const int kDY[6] = {0, 0, 1, -1, 0, 0};
const int kDZ[6] = {0, 0, 0, 0, 1, -1};

inline uint64_t chunkKey(int32_t cx, int32_t cz) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) |
           static_cast<uint32_t>(cz);
}

// How much light a step INTO this block costs. Air dims by 1, a
// transparent block dims by 1, an opaque one blocks outright (15).
inline int costThrough(uint8_t blockId) {
    if (blockId == 0) return 1;
    const uint8_t op = mcp_block_light_opacity(blockId);
    return op > 0 ? static_cast<int>(op) : 1;
}

// World-coordinate view over a chunk region.
//
// The chunk pointers are cached in a FLAT array indexed by chunk offset,
// not in a hash map: a full sweep touches hundreds of thousands of cells
// and each one does several chunk lookups, so an unordered_map here cost
// tens of milliseconds per relight (measured). A region spans at most a
// handful of chunks, so a small vector indexed by (cx - cx0, cz - cz0)
// turns those lookups into one subtraction and a bounds check.
class Region {
public:
    Region(World& lvl, int32_t x0, int32_t y0, int32_t z0,
           int32_t x1, int32_t y1, int32_t z1)
        : lvl_(lvl),
          x0_(x0), y0_(y0), z0_(z0),
          x1_(x1), y1_(y1), z1_(z1) {
        // The chunk span is WIDER than the box by one chunk on each side.
        // The seeding step has to read the light of the cell just OUTSIDE
        // the box (that is the boundary condition), and with an exact span
        // that read landed outside the span, returned null, and the whole
        // region was cleared and reseeded with nothing — which showed up
        // as light stopping dead at a chunk border.
        const int32_t cx0 = (x0 - 16) >> 4, cx1 = (x1 + 16) >> 4;
        const int32_t cz0 = (z0 - 16) >> 4, cz1 = (z1 + 16) >> 4;
        spanX_ = cx1 - cx0 + 1;
        spanZ_ = cz1 - cz0 + 1;
        spanCx0_ = cx0;
        spanCz0_ = cz0;
        // A missing neighbour is treated as "no light and no light
        // coming in"; when it does load, onChunkAdded() relights the
        // seam, so nothing is permanently lost.
        slots_.assign((size_t)spanX_ * (size_t)spanZ_, nullptr);
        for (int32_t cz = cz0; cz <= cz1; ++cz)
            for (int32_t cx = cx0; cx <= cx1; ++cx) {
                mcp_chunk_t* c = lvl_.chunks().getChunkIfLoaded(cx, cz);
                slots_[(size_t)(cz - cz0) * (size_t)spanX_ + (size_t)(cx - cx0)] =
                    c;
            }
    }

    mcp_chunk_t* chunkAt(int32_t wx, int32_t wz) {
        const int32_t ox = (wx >> 4) - spanCx0_;
        const int32_t oz = (wz >> 4) - spanCz0_;
        if (ox < 0 || ox >= spanX_ || oz < 0 || oz >= spanZ_) return nullptr;
        return slots_[(size_t)oz * (size_t)spanX_ + (size_t)ox];
    }

    bool contains(int32_t x, int32_t y, int32_t z) const {
        return x >= x0_ && x <= x1_ && y >= y0_ && y <= y1_ &&
               z >= z0_ && z <= z1_;
    }

    uint8_t blockId(int32_t x, int32_t y, int32_t z) {
        if (y < 0 || y >= kWorldHeight) return 0;
        mcp_chunk_t* c = chunkAt(x, z);
        return c ? mcp_chunk_get_block_id(c, x & 15, y, z & 15) : 0;
    }

    uint8_t sky(int32_t x, int32_t y, int32_t z) {
        mcp_chunk_t* c = chunkAt(x, z);
        return c ? mcp_chunk_get_sky_light(c, x & 15, y, z & 15) : 0;
    }

    void setSky(int32_t x, int32_t y, int32_t z, uint8_t v) {
        mcp_chunk_t* c = chunkAt(x, z);
        if (c) mcp_chunk_set_sky_light(c, x & 15, y, z & 15, v);
    }

    uint8_t blockLight(int32_t x, int32_t y, int32_t z) {
        mcp_chunk_t* c = chunkAt(x, z);
        return c ? mcp_chunk_get_block_light(c, x & 15, y, z & 15) : 0;
    }

    void setBlockLight(int32_t x, int32_t y, int32_t z, uint8_t v) {
        mcp_chunk_t* c = chunkAt(x, z);
        if (c) mcp_chunk_set_block_light(c, x & 15, y, z & 15, v);
    }

    // Highest terrain in the region, so the sweep does not waste time on
    // the empty sky above everything.
    int32_t maxTop() {
        if (topComputed_) return maxTop_;
        maxTop_ = 0;
        for (int32_t x = x0_; x <= x1_; ++x)
            for (int32_t z = z0_; z <= z1_; ++z) {
                mcp_chunk_t* c = chunkAt(x, z);
                if (!c) continue;
                const int32_t h =
                    mcp_chunk_get_height_map(c, x & 15, z & 15);
                if (h > maxTop_) maxTop_ = h;
            }
        topComputed_ = true;
        return maxTop_;
    }

private:
    World& lvl_;
    int32_t x0_, y0_, z0_, x1_, y1_, z1_;
    int32_t spanX_ = 1, spanZ_ = 1;
    int32_t spanCx0_ = 0, spanCz0_ = 0;
    std::vector<mcp_chunk_t*> slots_;
    bool topComputed_ = false;
    int32_t maxTop_ = 0;
};

} // namespace

// relight — recompute BOTH layers over the given box.
//
// The box is cleared first and then flooded back in, which is what makes
// a ceiling going up actually darken the room: there is no separate
// "remove light" pass to get wrong.
void LightEngine::relight(int32_t x0, int32_t y0, int32_t z0,
                          int32_t x1, int32_t y1, int32_t z1) {
    if (y0 < 0) y0 = 0;
    if (y1 > kWorldHeight - 1) y1 = kWorldHeight - 1;
    if (x0 > x1 || y0 > y1 || z0 > z1) return;

    Region r(lvl_, x0, y0, z0, x1, y1, z1);

    std::deque<Cell> skyQueue;
    std::deque<Cell> blockQueue;

    // --- clear + seed -------------------------------------------------
    // Sky seeds from the heightmap (anything at or above the first
    // opaque block sees the sky). Block light seeds from the block's
    // own emission, which is what finally makes torches work.
    //
    // Both layers also take a seed from the cell just OUTSIDE the box:
    // light already computed in the rest of the world still belongs to
    // this box's border cells, and dropping it would leave a dark ring
    // around every relit region.
    for (int32_t x = x0; x <= x1; ++x) {
        for (int32_t z = z0; z <= z1; ++z) {
            mcp_chunk_t* c = r.chunkAt(x, z);
            if (!c) continue;
            const int32_t h = mcp_chunk_get_height_map(c, x & 15, z & 15);

            for (int32_t y = y0; y <= y1; ++y) {
                const uint8_t id =
                    mcp_chunk_get_block_id(c, x & 15, y, z & 15);

                int sky = (y >= h) ? static_cast<int>(kMaxLight) : 0;
                int blk = static_cast<int>(mcp_block_light_emission(id));

                for (int d = 0; d < 6; ++d) {
                    const int32_t nx = x + kDX[d];
                    const int32_t ny = y + kDY[d];
                    const int32_t nz = z + kDZ[d];
                    if (r.contains(nx, ny, nz)) continue;  // not outside
                    if (ny < 0 || ny >= kWorldHeight) continue;
                    const int cost = costThrough(id);
                    const int inSky =
                        static_cast<int>(r.sky(nx, ny, nz)) - cost;
                    if (inSky > sky) sky = std::min(inSky, (int)kMaxLight);
                    const int inBlk =
                        static_cast<int>(r.blockLight(nx, ny, nz)) - cost;
                    if (inBlk > blk) blk = inBlk;
                }

                r.setSky(x, y, z, static_cast<uint8_t>(std::max(0, sky)));
                r.setBlockLight(x, y, z, static_cast<uint8_t>(std::max(0, blk)));
                if (sky > 0) skyQueue.push_back({x, y, z});
                if (blk > 0) blockQueue.push_back({x, y, z});
            }
        }
    }

    // --- flood, six directions ----------------------------------------
    // Y is included this time, so light reaches down into caves and
    // under overhangs instead of stopping at the ceiling edge.
    auto spread = [&](std::deque<Cell>& queue, bool isSky) {
        while (!queue.empty()) {
            const Cell cur = queue.front();
            queue.pop_front();
            const int level =
                isSky ? r.sky(cur.x, cur.y, cur.z)
                      : r.blockLight(cur.x, cur.y, cur.z);
            if (level <= 1) continue;

            for (int d = 0; d < 6; ++d) {
                const int32_t nx = cur.x + kDX[d];
                const int32_t ny = cur.y + kDY[d];
                const int32_t nz = cur.z + kDZ[d];
                // Only write inside the box; whatever is outside keeps
                // the value it already had.
                if (!r.contains(nx, ny, nz)) continue;
                const uint8_t id = r.blockId(nx, ny, nz);
                const int cand = level - costThrough(id);
                if (cand <= 0) continue;
                const int have =
                    isSky ? r.sky(nx, ny, nz) : r.blockLight(nx, ny, nz);
                if (cand <= have) continue;
                if (isSky) r.setSky(nx, ny, nz, (uint8_t)cand);
                else r.setBlockLight(nx, ny, nz, (uint8_t)cand);
                if (cand > 1) queue.push_back({nx, ny, nz});
            }
        }
    };
    spread(skyQueue, true);
    spread(blockQueue, false);
}

void LightEngine::relightAround(int32_t x, int32_t y, int32_t z,
                                int32_t radius) {
    if (radius < 1) radius = 1;
    relight(x - radius, y - radius, z - radius,
            x + radius, y + radius, z + radius);
}

void LightEngine::relightChunk(int32_t cx, int32_t cz, int32_t radius) {
    const int32_t bx = cx * 16, bz = cz * 16;
    // Probe the terrain first so the sweep stops just above the highest
    // block instead of walking 128 rows of empty sky per chunk. Anything
    // above the highest terrain is open sky and already reads 15.
    Region probe(lvl_, bx - radius, 0, bz - radius,
                 bx + 15 + radius, kWorldHeight - 1, bz + 15 + radius);
    const int32_t y1 = std::min(kWorldHeight - 1, probe.maxTop() + 1);

    relight(bx - radius, 0, bz - radius,
            bx + 15 + radius, y1, bz + 15 + radius);
}

void LightEngine::onChunkAdded(int32_t cx, int32_t cz) {
    // Called on the generation worker thread: queue only. Relighting
    // here would hold up generation, and touching ChunkManager from
    // inside setChunk() would deadlock on its mutex.
    std::lock_guard<std::mutex> lk(mtx_);
    pending_.emplace_back(cx, cz);
}

bool LightEngine::hasPending() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return !pending_.empty();
}

size_t LightEngine::processPending(int32_t budget) {
    std::deque<std::pair<int32_t, int32_t>> work;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        while (!pending_.empty() && work.size() < (size_t)budget) {
            work.push_back(pending_.front());
            pending_.pop_front();
        }
    }
    for (const auto& [cx, cz] : work) {
        // A chunk that got unloaded again before its turn is skipped by
        // the Region cache returning null everywhere; harmless.
        relightChunk(cx, cz, kChunkRelightRadius);
    }
    return work.size();
}

} // namespace cursestone::world