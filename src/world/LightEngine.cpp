/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "LightEngine.h"

#include "ChunkManager.h"
#include "World.h"
#include "block/Block.h"
#include "world/format/Chunk.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <deque>
#include <vector>

namespace cursestone::world {

namespace {

constexpr int32_t kWorldHeight = 128;

// One cell of a light flood queue.
struct Cell {
    int32_t x, y, z;
};

// Light is stored as a nibble and tops out at 15, so 15 blocks is the
// furthest anything can travel.
constexpr uint8_t kMaxLight = 15;

const int kDX[6] = {1, -1, 0, 0, 0, 0};
const int kDY[6] = {0, 0, 1, -1, 0, 0};
const int kDZ[6] = {0, 0, 0, 0, 1, -1};

inline uint64_t chunkKey(int32_t cx, int32_t cz) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) | static_cast<uint32_t>(cz);
}

// How much light a step INTO this block costs. Air dims by 1, a
// transparent block dims by 1, an opaque one blocks outright (15).
inline int costThrough(uint8_t blockId) {
    if (blockId == 0)
        return 1;
    const uint8_t op = cursestone::block::Block::lightOpacity(blockId);
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
    Region(World& lvl, int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1)
        : lvl_(lvl), x0_(x0), y0_(y0), z0_(z0), x1_(x1), y1_(y1), z1_(z1) {
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
                slots_[(size_t)(cz - cz0) * (size_t)spanX_ + (size_t)(cx - cx0)] = c;
            }
    }

    mcp_chunk_t* chunkAt(int32_t wx, int32_t wz) {
        const int32_t ox = (wx >> 4) - spanCx0_;
        const int32_t oz = (wz >> 4) - spanCz0_;
        if (ox < 0 || ox >= spanX_ || oz < 0 || oz >= spanZ_)
            return nullptr;
        return slots_[(size_t)oz * (size_t)spanX_ + (size_t)ox];
    }

    bool contains(int32_t x, int32_t y, int32_t z) const {
        return x >= x0_ && x <= x1_ && y >= y0_ && y <= y1_ && z >= z0_ && z <= z1_;
    }

    uint8_t blockId(int32_t x, int32_t y, int32_t z) {
        if (y < 0 || y >= kWorldHeight)
            return 0;
        mcp_chunk_t* c = chunkAt(x, z);
        return c ? mcp_chunk_get_block_id(c, x & 15, y, z & 15) : 0;
    }

    uint8_t blockLight(int32_t x, int32_t y, int32_t z) {
        mcp_chunk_t* c = chunkAt(x, z);
        return c ? mcp_chunk_get_block_light(c, x & 15, y, z & 15) : 0;
    }

    void setBlockLight(int32_t x, int32_t y, int32_t z, uint8_t v) {
        mcp_chunk_t* c = chunkAt(x, z);
        if (c)
            mcp_chunk_set_block_light(c, x & 15, y, z & 15, v);
    }

    // Highest terrain in the region, so the sweep does not waste time on
    // the empty sky above everything.
    int32_t maxTop() {
        if (topComputed_)
            return maxTop_;
        maxTop_ = 0;
        for (int32_t x = x0_; x <= x1_; ++x)
            for (int32_t z = z0_; z <= z1_; ++z) {
                mcp_chunk_t* c = chunkAt(x, z);
                if (!c)
                    continue;
                const int32_t h = mcp_chunk_get_height_map(c, x & 15, z & 15);
                if (h > maxTop_)
                    maxTop_ = h;
            }
        topComputed_ = true;
        return maxTop_;
    }

    // Every loaded chunk whose columns overlap the BOX (not the wider
    // seeding span). The seed phase writes every cell of every loaded
    // chunk column inside the box, so this is exactly the set of chunks
    // the sweep wrote; relight() uses it to flag (light_touched) and
    // re-queue the foreign chunks whose cells it deposited light into.
    template <class F>
    void forEachBoxChunk(F&& fn) {
        const int32_t bcx0 = x0_ >> 4, bcx1 = x1_ >> 4;
        const int32_t bcz0 = z0_ >> 4, bcz1 = z1_ >> 4;
        for (int32_t cz = bcz0; cz <= bcz1; ++cz)
            for (int32_t cx = bcx0; cx <= bcx1; ++cx)
                if (mcp_chunk_t* c = chunkAt(cx * 16, cz * 16))
                    fn(cx, cz, c);
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

// BFS spread for the block layer, constrained to writes inside the
// region box. Shared by the full sweep, the border pull and the
// interior flood — the only difference between those paths is WHICH
// cells are seeded and how wide the box is. Sky light is global
// (every cell reads 15) and is never written here.
void spreadLayer(Region& r, std::deque<Cell>& queue) {
    while (!queue.empty()) {
        const Cell cur = queue.front();
        queue.pop_front();
        const int level = r.blockLight(cur.x, cur.y, cur.z);
        if (level <= 1)
            continue;

        for (int d = 0; d < 6; ++d) {
            const int32_t nx = cur.x + kDX[d];
            const int32_t ny = cur.y + kDY[d];
            const int32_t nz = cur.z + kDZ[d];
            // Only write inside the box; whatever is outside keeps
            // the value it already had.
            if (!r.contains(nx, ny, nz))
                continue;
            if (ny < 0 || ny >= kWorldHeight)
                continue;
            const uint8_t id = r.blockId(nx, ny, nz);
            const int cand = level - costThrough(id);
            if (cand <= 0)
                continue;
            const int have = r.blockLight(nx, ny, nz);
            if (cand <= have)
                continue;
            r.setBlockLight(nx, ny, nz, (uint8_t)cand);
            if (cand > 1)
                queue.push_back({nx, ny, nz});
        }
    }
}

} // namespace

// relight — recompute the BLOCK layer over the given box.
//
// Sky light is global (every cell reads 15, see mcp_chunk_propagate_sky_light
// in the C chunk layer), so this sweep no longer clears, seeds or floods
// the sky layer at all — nothing can darken it, and writing it here would
// only re-create the shadows this server deliberately does not have.
// Building a roof keeps the room bright; that is the requested behaviour.
//
// The block layer still gets the full clear + outside-border seed + flood
// treatment: there is no separate "remove light" pass, so a torch going
// away simply lowers the seed value around it.
void LightEngine::relight(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1,
                          int32_t homeCx, int32_t homeCz) {
    if (y0 < 0)
        y0 = 0;
    if (y1 > kWorldHeight - 1)
        y1 = kWorldHeight - 1;
    if (x0 > x1 || y0 > y1 || z0 > z1)
        return;

    Region r(lvl_, x0, y0, z0, x1, y1, z1);

    std::deque<Cell> blockQueue;

    // --- clear + seed -------------------------------------------------
    // Block light seeds from the block's own emission, which is what
    // makes torches work, and on the box border also from the cell just
    // OUTSIDE the box: light already computed in the rest of the world
    // still belongs to this box's border cells, and dropping it would
    // leave a dark ring around every relit region.
    for (int32_t x = x0; x <= x1; ++x) {
        const bool xBorder = (x == x0 || x == x1);
        for (int32_t z = z0; z <= z1; ++z) {
            mcp_chunk_t* c = r.chunkAt(x, z);
            if (!c)
                continue;
            const int lx = x & 15, lz = z & 15;
            const bool border = xBorder || z == z0 || z == z1;

            c->dirty = true;

            for (int secY = y0 >> 4; secY <= y1 >> 4; ++secY) {
                const int ya = std::max(y0, secY * 16);
                const int yb = std::min(y1, secY * 16 + 15);
                mcp_chunk_section_t* s = c->sections[secY];
                if (!s)
                    continue; // no blocks here: emission 0, nothing to clear
                for (int y = ya; y <= yb; ++y) {
                    const uint8_t id = s->blocks[lx | (lz << 4) | ((y & 15) << 8)];
                    int blk = static_cast<int>(cursestone::block::Block::lightEmission(id));

                    if (border || y == y0 || y == y1) {
                        for (int d = 0; d < 6; ++d) {
                            const int32_t nx = x + kDX[d];
                            const int32_t ny = y + kDY[d];
                            const int32_t nz = z + kDZ[d];
                            if (r.contains(nx, ny, nz))
                                continue; // not outside
                            if (ny < 0 || ny >= kWorldHeight)
                                continue;
                            const int cost = costThrough(id);
                            const int inBlk = static_cast<int>(r.blockLight(nx, ny, nz)) - cost;
                            if (inBlk > blk)
                                blk = inBlk;
                        }
                    }

                    mcp_section_set_nibble(s->block_light, lx, y, lz,
                                           static_cast<uint8_t>(std::max(0, blk)));
                    if (blk > 0)
                        blockQueue.push_back({x, y, z});
                }
            }
        }
    }

    // --- flood, six directions ----------------------------------------
    spreadLayer(r, blockQueue);

    // Foreign-write bookkeeping: the sweep wrote every cell of every
    // loaded chunk overlapping the box. The home chunk was fully
    // recomputed, but each FOREIGN chunk just received deposited light
    // (and its own interior was never re-flooded) — flag it so its next
    // border entry runs the incremental interior flood, and queue the
    // entry so the deposit cannot freeze mid-chunk. Monotone rising
    // values keep the chain finite.
    if (homeCx != INT32_MIN) {
        r.forEachBoxChunk([&](int32_t ccx, int32_t ccz, mcp_chunk_t* c) {
            if (ccx == homeCx && ccz == homeCz)
                return;
            c->light_touched = 1;
            queueEntry(ccx, ccz, /*full=*/false);
        });
    }
}

void LightEngine::relightAround(int32_t x, int32_t y, int32_t z, int32_t radius) {
    if (radius < 1)
        radius = 1;
    relight(x - radius, y - radius, z - radius, x + radius, y + radius, z + radius,
            x >> 4, z >> 4);
}

void LightEngine::relightChunk(int32_t cx, int32_t cz, int32_t radius) {
    const int32_t bx = cx * 16, bz = cz * 16;
    // Probe the terrain first so the sweep stops just above the highest
    // block instead of walking 128 rows of empty sky per chunk. Anything
    // above the highest terrain is open sky and already reads 15.
    Region probe(lvl_, bx - radius, 0, bz - radius, bx + 15 + radius, kWorldHeight - 1,
                 bz + 15 + radius);
    const int32_t y1 = std::min(kWorldHeight - 1, probe.maxTop() + 1);

    relight(bx - radius, 0, bz - radius, bx + 15 + radius, y1, bz + 15 + radius, cx, cz);
}

void LightEngine::queueEntry(int32_t cx, int32_t cz, bool full) {
    std::lock_guard<std::mutex> lk(mtx_);
    const uint64_t key = chunkKey(cx, cz);
    auto it = pending_keys_.find(key);
    if (it != pending_keys_.end()) {
        if (full) {
            for (auto& e : pending_) {
                if (e.cx == cx && e.cz == cz) {
                    e.full = true;
                    break;
                }
            }
        }
        return;
    }
    pending_keys_.insert(it, key);
    pending_.push_back({cx, cz, full});
}

void LightEngine::onChunkAdded(int32_t cx, int32_t cz, bool full) {
    // Called on the generation worker thread: queue only. Relighting
    // here would hold up generation, and touching ChunkManager from
    // inside setChunk() would deadlock on its mutex.
    queueEntry(cx, cz, full);
}

bool LightEngine::hasPending() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return !pending_.empty();
}

size_t LightEngine::processPending(int32_t budget, int32_t max_ms) {
    std::deque<PendingEntry> work;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        while (!pending_.empty() && work.size() < (size_t)budget) {
            work.push_back(pending_.front());
            pending_.pop_front();
            pending_keys_.erase(chunkKey(work.back().cx, work.back().cz));
        }
    }
    const auto t0 = std::chrono::steady_clock::now();
    size_t done = 0;
    for (const PendingEntry& e : work) {
        // A chunk that got unloaded again before its turn is skipped by
        // the Region cache returning null everywhere; harmless.
        if (e.full) {
            // Blocks genuinely changed inside a live chunk after install
            // (generation spill batch): re-flood the chunk AND one full
            // light range into every neighbour, exactly as before.
            relightChunk(e.cx, e.cz, kChunkRelightRadius);
        } else {
            runBorderEntry(e.cx, e.cz);
        }
        ++done;
        // Time budget: stop before STARTING another entry once the tick's
        // share is spent (a started entry always completes). Whatever is
        // left stays queued for the next tick.
        if (max_ms > 0) {
            const double spent =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                    .count();
            if (spent >= (double)max_ms)
                break;
        }
    }
    if (done < work.size()) {
        std::lock_guard<std::mutex> lk(mtx_);
        for (size_t i = work.size(); i-- > done;) {
            const PendingEntry& e = work[i];
            const uint64_t key = chunkKey(e.cx, e.cz);
            if (pending_keys_.insert(key).second)
                pending_.push_front(e);
        }
    }
    return done;
}

// One Border entry: pull the neighbours' light in, spread any light a
// neighbour's sweep or push deposited inside us, push our light out, and
// re-queue neighbours whose cells the push raised. Without the re-queue +
// interior flood, light that crosses a chunk border mid-way dies: the
// depositing sweep writes its cells directly and nothing would spread
// them further inward. Values only ever RISE and light is capped at 15,
// so the chain terminates after a couple of passes at most.
void LightEngine::runBorderEntry(int32_t cx, int32_t cz) {
    mcp_chunk_t* c = lvl_.chunks().getChunkIfLoaded(cx, cz);
    if (c && c->light_populated) {
        borderPull(cx, cz);
        // A neighbour's sweep or push wrote cells inside this chunk (flag
        // set by relight()'s foreign-write tracking / pushBorder). Those
        // cells need their light spread further inward — the pull only
        // floods from the border walls, so without this the deposited
        // light freezes where it landed (measured: sky stuck at 5 where
        // the fixed point says 9, two borders away from the source).
        if (c->light_touched) {
            c->light_touched = 0;
            interiorFlood(cx, cz);
        }
    } else {
        // A chunk whose stored light is NOT trustworthy (never lit —
        // e.g. an old save with LightPopulated=0) takes the clearing
        // sweep, which re-derives the interior from scratch.
        relightChunk(cx, cz, 0);
        if (c)
            c->light_touched = 0;
    }
    std::vector<std::pair<int32_t, int32_t>> touched;
    pushBorder(cx, cz, &touched);
    for (const auto& [tcx, tcz] : touched)
        onChunkAdded(tcx, tcz, /*full=*/false);
}

bool LightEngine::flushPending(int32_t cx, int32_t cz) {
    PendingEntry e;
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        const uint64_t key = chunkKey(cx, cz);
        if (pending_keys_.erase(key) == 0)
            return false;
        for (auto it = pending_.begin(); it != pending_.end(); ++it) {
            if (it->cx == cx && it->cz == cz) {
                e = *it;
                pending_.erase(it);
                found = true;
                break;
            }
        }
    }
    if (!found)
        return false;
    if (e.full) {
        relightChunk(cx, cz, kChunkRelightRadius);
    } else {
        runBorderEntry(cx, cz);
    }
    return true;
}

// Chunk-local region probe: builds the Region over one chunk and returns
// the highest terrain in it, +1 margin, clamped to the world top. The
// border passes bound their vertical walks with this.
static int32_t chunkProbeTop(World& lvl, int32_t cx, int32_t cz) {
    const int32_t bx = cx * 16, bz = cz * 16;
    Region probe(lvl, bx, 0, bz, bx + 15, kWorldHeight - 1, bz + 15);
    return std::min(kWorldHeight - 1, probe.maxTop() + 1);
}

// Incremental interior flood: re-push every lit cell of the chunk
// (block layer) and let the flood raise whatever can still be raised,
// writing ONLY inside the chunk. Runs when a neighbour's sweep or push
// deposited light here — the deposited cells are not border walls, so
// borderPull's wall-seeded flood would never touch them. Seeds are
// collected with a full accessor read of the chunk's light nibbles.
void LightEngine::interiorFlood(int32_t cx, int32_t cz) {
    const int32_t bx = cx * 16, bz = cz * 16;
    Region r(lvl_, bx, 0, bz, bx + 15, kWorldHeight - 1, bz + 15);
    const int32_t yTop = r.maxTop() + 1;

    std::deque<Cell> blockQueue;
    for (int x = bx; x <= bx + 15; ++x)
        for (int z = bz; z <= bz + 15; ++z)
            for (int y = 0; y <= yTop; ++y) {
                const uint8_t b = r.blockLight(x, y, z);
                if (b > 1)
                    blockQueue.push_back({x, y, z});
            }
    spreadLayer(r, blockQueue);
}

// Pull: raise the chunk's own border cells from the block light just
// outside the chunk, then flood inward from whichever cells actually
// rose. The interior keeps whatever the generation worker computed —
// this pass only ever ADDS the cross-border flow the single-chunk
// worker pass could not see. Wall cells cannot pull sky light (the sky
// layer is global and never written), so only the block channel moves.
void LightEngine::borderPull(int32_t cx, int32_t cz) {
    const int32_t bx = cx * 16, bz = cz * 16;
    Region r(lvl_, bx, 0, bz, bx + 15, kWorldHeight - 1, bz + 15);
    const int32_t yTop = chunkProbeTop(lvl_, cx, cz);

    std::deque<Cell> blockQueue;

    for (int y = 0; y <= yTop; ++y) {
        for (int i = 0; i < 16; ++i) {
            // The four wall lines: (0,i) (15,i) (i,0) (i,15) in chunk
            // local coordinates. The outside offset depends on WHICH
            // wall the line is, not on the coordinate value — a corner
            // cell belongs to two walls and must push across both.
            const int lxs[4] = {0, 15, i, i};
            const int lzs[4] = {i, i, 0, 15};
            for (int w = 0; w < 4; ++w) {
                const int32_t x = bx + lxs[w], z = bz + lzs[w];
                int32_t ox = x, oz = z;
                if (w == 0)
                    ox = x - 1;
                else if (w == 1)
                    ox = x + 1;
                else if (w == 2)
                    oz = z - 1;
                else
                    oz = z + 1;
                const uint8_t id = r.blockId(x, y, z);
                const int cost = costThrough(id);

                const int inBlk = (int)r.blockLight(ox, y, oz) - cost;
                if (inBlk > (int)r.blockLight(x, y, z)) {
                    r.setBlockLight(x, y, z, (uint8_t)std::max(0, std::min(inBlk, (int)kMaxLight)));
                    blockQueue.push_back({x, y, z});
                }
            }
        }
    }
    spreadLayer(r, blockQueue);
}

// Push: raise the neighbours' cells the chunk can improve (block
// layer). Only the wall cells can hand light across a border (one
// step), and the push writes the neighbour's border cell DIRECTLY — the
// neighbour is flagged light_touched and returned in `touched` so its
// queued entry spreads the deposit inward with the incremental interior
// flood. The vertical walk runs the full world height: a taller
// neighbour can have unlit cells level with our torch glow, and
// skipping them would freeze light the fixed point says should flow.
void LightEngine::pushBorder(int32_t cx, int32_t cz,
                             std::vector<std::pair<int32_t, int32_t>>* touched) {
    const int32_t bx = cx * 16, bz = cz * 16;
    Region r(lvl_, bx, 0, bz, bx + 15, kWorldHeight - 1, bz + 15);

    for (int y = 0; y < kWorldHeight; ++y) {
        for (int i = 0; i < 16; ++i) {
            const int lxs[4] = {0, 15, i, i};
            const int lzs[4] = {i, i, 0, 15};
            for (int w = 0; w < 4; ++w) {
                const int32_t x = bx + lxs[w], z = bz + lzs[w];
                int32_t ox = x, oz = z;
                if (w == 0)
                    ox = x - 1;
                else if (w == 1)
                    ox = x + 1;
                else if (w == 2)
                    oz = z - 1;
                else
                    oz = z + 1;
                // A missing neighbour chunk makes setBlockLight a no-op;
                // when it loads, its own arrival entry pulls this border.
                const int cost = costThrough(r.blockId(ox, y, oz));

                const int candBlk = (int)r.blockLight(x, y, z) - cost;
                if (candBlk > (int)r.blockLight(ox, y, oz)) {
                    r.setBlockLight(ox, y, oz, (uint8_t)std::max(0, std::min(candBlk, (int)kMaxLight)));
                    mcp_chunk_t* nc = r.chunkAt(ox, oz);
                    if (nc)
                        nc->light_touched = 1;
                    if (touched)
                        touched->push_back({ox >> 4, oz >> 4});
                }
            }
        }
    }
}

} // namespace cursestone::world