/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
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
#include "Player.h"
#include "world/World.h"
#include "world/format/Chunk.h"
#include "scheduler/AsyncPool.h"
#include "network/protocol/FullChunkDataPacket.h"
#include "diagnostics/ChestDebug.h"
#include "tile/Tile.h"
#include "utils/CRaii.h"
#include <cstdlib>
#include <utility>
#include <vector>

namespace cursestone {

bool Player::isTrackingChunk(int32_t cx, int32_t cz) const {
    int64_t hash = ((int64_t)cx << 32) | ((uint32_t)cz);
    std::lock_guard<std::mutex> lk(chunks_mtx_);
    return used_chunks_.count(hash) > 0;
}

bool Player::knowsMob(int64_t eid) const {
    std::lock_guard<std::mutex> lk(chunks_mtx_);
    return known_mobs_.count(eid) > 0;
}

void Player::rememberMob(int64_t eid) {
    std::lock_guard<std::mutex> lk(chunks_mtx_);
    known_mobs_.insert(eid);
}

void Player::forgetMob(int64_t eid) {
    std::lock_guard<std::mutex> lk(chunks_mtx_);
    known_mobs_.erase(eid);
}

void Player::clearKnownMobs() {
    std::lock_guard<std::mutex> lk(chunks_mtx_);
    known_mobs_.clear();
}

// ============================================================
// Phase 5 — sendChunksAroundSpawn (spiral order, like the reference server)
// ============================================================
// Fix #3: Spiral algorithm extracted to buildChunkSpiral() — single
// source of truth shared by sendChunksAroundSpawn() and onUpdate().
// Fix #4: sendChunksAroundSpawn() not longer sends chunks directly.
// It only populates chunk_queue_ with spiral coordinates. The current
// streaming happens in onUpdate() at MAX_CHUNKS_PER_TICK=4 per tick.
// ============================================================

// Vanilla-style square spiral: `radius` is now to actual per-axis radius,
// not the old "vd*vd total points" size. This produces every offset in
// [-radius, radius] on both axes — (2*radius+1)^2 points total — same
// shape vanilla/reference use for view-distance, ordered center-outward so
// the closest (most important) chunks are generated/sent first.
//
// FIX (see chat diagnosis): the previous version took `vd` as the total
// point count and looped `i < vd*vd`, which only ever reached about
// vd/2 chunks out on each axis — half the configured view-distance,
// and server.cpp had to compensate with to hardcoded `viewDistance()/2`
// for the unload sweep. That compensation is removed now that this
// returns to true radius.
std::vector<std::pair<int32_t, int32_t>> Player::buildChunkSpiral(int radius) {
    std::vector<std::pair<int32_t, int32_t>> order;
    if (radius <= 0) {
        order.emplace_back(0, 0);
        return order;
    }
    int side = 2 * radius + 1;
    order.reserve((size_t)side * (size_t)side);
    int layer = 1, leg = 0, x = 0, z = 0;
    order.emplace_back(x, z);
    int total = side * side;
    for (int i = 1; i < total; ++i) {
        switch (leg) {
        case 0:
            ++x;
            if (x == layer)
                ++leg;
            break;
        case 1:
            ++z;
            if (z == layer)
                ++leg;
            break;
        case 2:
            --x;
            if (-x == layer)
                ++leg;
            break;
        case 3:
            --z;
            if (-z == layer) {
                leg = 0;
                ++layer;
            }
            break;
        }
        order.emplace_back(x, z);
    }
    return order;
}

void Player::sendChunksAroundSpawn() {
    if (!getWorld()) {
        doFirstSpawn();
        return;
    }
    int centerX = (int)pos_.x >> 4;
    int centerZ = (int)pos_.z >> 4;
    int vd = cfg_.view_distance;

    // Fix #3: use shared buildChunkSpiral() instead of inline copy.
    auto order = buildChunkSpiral(vd);

    // Fix #4: ONLY populate chunk_queue_ — do NOT send chunks directly.
    // The streaming happens in onUpdate() at MAX_CHUNKS_PER_TICK=4/tick.
    // This prevents blocking the main thread for 64 chunks at login.
    {
        std::lock_guard<std::mutex> lk(queue_mtx_);
        chunk_queue_.clear();
        for (auto& pr : order) {
            chunk_queue_.emplace_back(centerX + pr.first, centerZ + pr.second);
        }
    }

    // bug FIX ("not puedo take out items of the creativo the first time that
    // entro, but yes works if salgo and vuelvo to enter"): this used to
    // call doFirstSpawn() immediately here — before to single chunk had
    // been streamed — on the theory that "the client will see chunks
    // arriving gradually" (true for the era reference, which doesn't gate spawn on
    // chunks). But doFirstSpawn() is also where PLAYER_SPAWN,
    // sendCreativeInventory() and sendInventory() (the player's OWN
    // inventory ContainerSetContentPacket) go out. Sending those with
    // zero terrain loaded works fine for to RETURNING player, because
    // their spawn chunks are already generated/cached and stream in
    // almost instantly regardless — but for to brand-new player at to
    // never-before-visited spawn point, chunk GENERATION is async and
    // can take noticeably longer (see getChunkOrRequestAsync() in
    // onUpdate()). The MCPE 0.15.x client doesn't fully "arm" its own
    // inventory window until it has actually finished loading into the
    // world, so an inventory-content packet sent this early can be
    // silently dropped/ignored while the client is still on the loading
    // screen. The server-side item is still added to inv_ (that's why
    // it "appears" on the next login, once the full inventory resync at
    // doFirstSpawn() lands on to client that's already loaded) but the
    // client never rendered the confirmation the first time.
    //
    // actual reference server never has this race: doFirstSpawn() there is only
    // called once chunkLoadCount >= spawnThreshold (see player's
    // orderChunks()) — i.e. once to handful of chunks around the player
    // have actually reached the client. We replicate that gate instead
    // of spawning immediately: onUpdate() now streams chunks BEFORE
    // spawned_ is true too, and only calls doFirstSpawn() once
    // FIRST_SPAWN_CHUNK_THRESHOLD chunks have gone out.
    awaiting_first_spawn_.store(true, std::memory_order_relaxed);
}

// ============================================================
// sendChunkTiles — forwards the BlockEntityData of all the tiles of the
// chunk to this player. The reference server takes precedence the tiles next to with the chunk; without
// esto, when reconnecting the client receives the block of the sign but not its
// NBT, this way that the text not is renders until someone it edit again.
// ============================================================
void Player::sendChunkTiles(int32_t cx, int32_t cz) {
    if (!getWorld())
        return;
    // Every stream must carry the chunk's CURRENT tile set: the client may
    // have dropped this chunk earlier (render-radius change, leaving and
    // coming back) and taken its block actors with it, and tile-needing
    // blocks can appear at any time (structure population writing into a
    // resident chunk, a furnace placed before the round trip). Re-scan the
    // resident blocks so tiles created after the first hydration still get
    // their BlockEntityData out — in the chunk-data-then-tile-NBTs order
    // the client binds actors in. Without this, a re-streamed furnace block
    // reaches the client with no actor and opening its screen crashes the
    // game (the village-furnace crash).
    //
    // PERF: this used to force-invalidate the chunk's tile hydration on
    // EVERY stream, making getTilesInChunk() below re-walk all 32768
    // block cells even for a chunk that had not changed since its last
    // scan. The chunk now carries a block-edit serial (bumped by every
    // live block write — population spills, player edits, liquid), and
    // ensureTilesLoaded() re-scans only when that serial moved, so the
    // village-furnace guarantee still holds at a 32-bit-compare cost in
    // the steady state.
    auto tiles = getWorld()->getTilesInChunk(cx, cz);
    for (auto& t : tiles) {
        if (!t)
            continue;
        const auto& p = t->position();
        sendBlockEntityData((int32_t)p.x, (int32_t)p.y, (int32_t)p.z, *t, this);
    }
}

bool Player::isChunkLoaded(int cx, int cz) const {
    int64_t hash = ((int64_t)cx << 32) | ((uint32_t)cz);
    std::lock_guard<std::mutex> lk(chunks_mtx_);
    return used_chunks_.count(hash) > 0;
}
bool Player::isChunkLoading(int cx, int cz) const {
    (void)cx;
    (void)cz; // stub — not yet tracking in-flight chunk sends
    return false;
}
void Player::unloadChunk(int cx, int cz) {
    int64_t hash = ((int64_t)cx << 32) | ((uint32_t)cz);
    std::lock_guard<std::mutex> lk(chunks_mtx_);
    used_chunks_.erase(hash);
}
void Player::sendChunk(int cx, int cz) {
    if (!getWorld())
        return;
    auto& chunks = getWorld()->chunks();
    mcp_chunk_t* ch = chunks.getChunk(cx, cz);
    if (!ch)
        return;
    // Finalize queued light work first — a protocol 84 client keeps the
    // light bytes it got in this packet, so the chunk must leave with its
    // border pass already applied (same rule as the onUpdate stream).
    getWorld()->flushLightForChunk(cx, cz);
    size_t outLen = 0;
    utils::BufferPtr buf(mcp_chunk_network_serialize(ch, &outLen));
    if (!buf)
        return;
    if (outLen == 0)
        return;
    auto pk = std::make_shared<network::protocol::FullChunkDataPacket>();
    pk->chunkX = cx;
    pk->chunkZ = cz;
    pk->data.assign(buf.get(), buf.get() + outLen);
    diagnostics::ChestDebug::onChunkBuilt(*this, cx, cz, pk->data.data(), pk->data.size());
    sendPacket(pk);
    sendChunkTiles(cx, cz);
    int64_t hash = ((int64_t)cx << 32) | ((uint32_t)cz);
    {
        std::lock_guard<std::mutex> lk(chunks_mtx_);
        used_chunks_.insert(hash);
    }
    // Mobs of this chunk ride the stream: a viewer that just received
    // the terrain gets their standing entities in the same breath. The
    // World dedups against this player's known-mob set, so the call is
    // free for chunks whose mobs the client already holds. Runs with no
    // lock held — sendMobsInChunk takes the world's mob lock and then
    // chunks_mtx_ again in rememberMob.
    if (getWorld())
        getWorld()->sendMobsInChunk(cx, cz, this);
}

// ============================================================
// Async chunk encode pipeline
// ============================================================
// The tick thread stops paying mcp_chunk_network_serialize()'s
// transposition (~83 KB of cell-by-cell nibble work per chunk): it takes
// a memcpy-bound snapshot — the tick thread is the only writer of
// resident chunk data, so the copy is a consistent immutable view — and
// an AsyncPool worker produces the wire payload from it. Workers touch
// ONLY their snapshot and the pipeline; never the Player, the World or
// ChunkManager (the pipeline is a shared_ptr, so a disconnect or a
// shutdown can never strand a worker on freed state).
//
// Ordering is preserved per player: flushChunkEncodes() sends strictly
// from the front of the slot deque, so a later chunk that finishes
// encoding first waits for the head slot to complete — the per-player
// reorder buffer.
// ============================================================
void Player::submitChunkEncode(mcp_chunk_t* ch, int32_t cx, int32_t cz) {
    if (!ch)
        return;
    if (!chunk_encodes_)
        chunk_encodes_ = std::make_shared<ChunkEncodePipeline>();
    ChunkEncodePipeline& pipe = *chunk_encodes_;

    {
        std::lock_guard<std::mutex> lk(pipe.mtx);
        for (const ChunkEncodeSlot& s : pipe.slots)
            if (s.cx == cx && s.cz == cz)
                return; // Encode already pending or ready-but-unflushed.
        if (pipe.slots.size() >= kMaxOutstandingChunkEncodes)
            return; // Reorder buffer full — retried on a later tick.
    }

    // Snapshot on the tick thread (memcpy-bound: at most 8 sections of
    // ~10 KB each — see Chunk.h). The tick thread is the only writer of
    // resident chunk data, so this copy is the immutable view the worker
    // encodes; the worker never reads the live chunk.
    mcp_chunk_wire_snapshot_t* snap = mcp_chunk_wire_snapshot_create(ch);
    if (!snap)
        return; // OOM — the chunk stays unsent and is retried next tick.

    uint64_t seq;
    {
        std::lock_guard<std::mutex> lk(pipe.mtx);
        seq = pipe.next_seq++;
        ChunkEncodeSlot slot;
        slot.seq = seq;
        slot.cx = cx;
        slot.cz = cz;
        pipe.slots.push_back(std::move(slot));
    }

    // Fills the slot once the payload exists. Captures the pipeline
    // shared_ptr BY VALUE: on the worker path this copy is what keeps the
    // pipeline alive past a disconnect (or past a re-stream reset — the
    // result then lands in the orphaned old pipeline and is never
    // flushed), and the slot lookup keeps working even if this player is
    // already gone.
    std::shared_ptr<ChunkEncodePipeline> pipe_ref = chunk_encodes_;
    auto complete = [pipe_ref, seq](uint8_t* buf, size_t len,
                                    mcp_chunk_wire_snapshot_t* s) {
        mcp_chunk_wire_snapshot_free(s);
        utils::BufferPtr wire(buf);
        std::lock_guard<std::mutex> lk(pipe_ref->mtx);
        for (ChunkEncodeSlot& slot : pipe_ref->slots) {
            if (slot.seq == seq) {
                slot.wire = std::move(wire);
                slot.wire_len = len;
                slot.ready = true;
                break;
            }
        }
    };

    auto* pool = srv_ ? srv_->getAsyncPool() : nullptr;
    if (pool && pool->submit([complete, snap]() {
            size_t len = 0;
            uint8_t* buf = mcp_chunk_snapshot_network_serialize(snap, &len);
            complete(buf, len, snap);
        }))
        return; // In flight; flushChunkEncodes sends it when ready.

    // Pool unavailable or stopping — encode inline so the chunk still
    // goes out (same fallback philosophy as getChunkOrRequestAsync).
    size_t len = 0;
    uint8_t* buf = mcp_chunk_snapshot_network_serialize(snap, &len);
    complete(buf, len, snap);
}

void Player::flushChunkEncodes(int max_chunks, size_t max_bytes) {
    if (!chunk_encodes_)
        return;
    ChunkEncodePipeline& pipe = *chunk_encodes_;
    int sent = 0;
    size_t bytes = 0;
    for (;;) {
        if (sent >= max_chunks)
            return; // Arrival pacing: same adaptive cap the submit side used.
        if (sent > 0 && bytes >= max_bytes)
            return; // Byte budget spent — the rest resumes next tick.

        ChunkEncodeSlot slot;
        {
            std::lock_guard<std::mutex> lk(pipe.mtx);
            if (pipe.slots.empty() || !pipe.slots.front().ready)
                return; // Nothing ready — or the head encode is still in
                        // flight: a later chunk that finished first must
                        // WAIT here, that is the whole ordering contract.
            slot = std::move(pipe.slots.front());
            pipe.slots.pop_front();
        }

        if (!slot.wire || slot.wire_len == 0)
            continue; // Worker-side OOM: the chunk was never marked used,
                      // so a later tick re-submits it (same retry as the
                      // synchronous path's null-buffer case).

        auto pk = std::make_shared<network::protocol::FullChunkDataPacket>();
        pk->chunkX = slot.cx;
        pk->chunkZ = slot.cz;
        pk->data.assign(slot.wire.get(), slot.wire.get() + slot.wire_len);
        diagnostics::ChestDebug::onChunkBuilt(*this, slot.cx, slot.cz, pk->data.data(),
                                              pk->data.size());
        sendPacket(pk);
        sendChunkTiles(slot.cx, slot.cz);
        int64_t hash = ((int64_t)slot.cx << 32) | ((uint32_t)slot.cz);
        {
            std::lock_guard<std::mutex> lk(chunks_mtx_);
            used_chunks_.insert(hash);
        }
        sent++;
        bytes += slot.wire_len;
        // Mobs of this chunk ride the stream (deduped per viewer by the
        // World). No lock held here — see sendChunk for the lock-order note.
        if (getWorld())
            getWorld()->sendMobsInChunk(slot.cx, slot.cz, this);
    }
}
} // namespace cursestone
