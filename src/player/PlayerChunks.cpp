/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
#include "Player.h"
#include "world/World.h"
#include "world/format/Chunk.h"
#include "network/protocol/FullChunkDataPacket.h"
#include "tile/Tile.h"
#include <cstdlib>
#include <utility>
#include <vector>

namespace cursestone {

bool Player::isTrackingChunk(int32_t cx, int32_t cz) const {
    int64_t hash = ((int64_t)cx << 32) | ((uint32_t)cz);
    std::lock_guard<std::mutex> lk(chunks_mtx_);
    return used_chunks_.count(hash) > 0;
}

// ============================================================
// Phase 5 — sendChunksAroundSpawn (spiral order, like PMMP)
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
// shape vanilla/PMMP use for view-distance, ordered center-outward so
// the closest (most important) chunks are generated/sent first.
//
// FIX (see chat diagnosis): the previous version took `vd` as the total
// point count and looped `i < vd*vd`, which only ever reached about
// vd/2 chunks out on each axis — half the configured view-distance,
// and server.cpp had to compensate with to hardcoded `viewDistance()/2`
// for the unload sweep. That compensation is removed now that this
// returns to true radius.
std::vector<std::pair<int32_t,int32_t>>
Player::buildChunkSpiral(int radius) {
    std::vector<std::pair<int32_t,int32_t>> order;
    if (radius <= 0) { order.emplace_back(0, 0); return order; }
    int side = 2 * radius + 1;
    order.reserve((size_t)side * (size_t)side);
    int layer = 1, leg = 0, x = 0, z = 0;
    order.emplace_back(x, z);
    int total = side * side;
    for (int i = 1; i < total; ++i) {
        switch (leg) {
            case 0: ++x; if (x == layer) ++leg; break;
            case 1: ++z; if (z == layer) ++leg; break;
            case 2: --x; if (-x == layer) ++leg; break;
            case 3: --z; if (-z == layer) { leg = 0; ++layer; } break;
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
    // arriving gradually" (true for Nukkit, which doesn't gate spawn on
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
    // actual PMMP never has this race: doFirstSpawn() there is only
    // called once chunkLoadCount >= spawnThreshold (see player.php's
    // orderChunks()) — i.e. once to handful of chunks around the player
    // have actually reached the client. We replicate that gate instead
    // of spawning immediately: onUpdate() now streams chunks BEFORE
    // spawned_ is true too, and only calls doFirstSpawn() once
    // FIRST_SPAWN_CHUNK_THRESHOLD chunks have gone out.
    awaiting_first_spawn_.store(true, std::memory_order_relaxed);
}

// ============================================================
// sendChunkTiles — forwards the BlockEntityData of all the tiles of the
// chunk to this player. PMMP takes precedence the tiles next to with the chunk; without
// esto, when reconnecting the client receives the block of the sign but not its
// NBT, this way that the text not is renders until someone it edit again.
// ============================================================
void Player::sendChunkTiles(int32_t cx, int32_t cz) {
    if (!getWorld()) return;
    auto tiles = getWorld()->getTilesInChunk(cx, cz);
    for (auto& t : tiles) {
        if (!t) continue;
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
    (void)cx; (void)cz;  // stub — not yet tracking in-flight chunk sends
    return false;
}
void Player::unloadChunk(int cx, int cz) {
    int64_t hash = ((int64_t)cx << 32) | ((uint32_t)cz);
    std::lock_guard<std::mutex> lk(chunks_mtx_);
    used_chunks_.erase(hash);
}
void Player::sendChunk(int cx, int cz) {
    if (!getWorld()) return;
    auto& chunks = getWorld()->chunks();
    mcp_chunk_t* ch = chunks.getChunk(cx, cz);
    if (!ch) return;
    size_t outLen = 0;
    uint8_t* buf = mcp_chunk_network_serialize(ch, &outLen);
    if (!buf || outLen == 0) return;
    auto pk = std::make_shared<network::protocol::FullChunkDataPacket>();
    pk->chunkX = cx; pk->chunkZ = cz;
    pk->data.assign(buf, buf + outLen);
    free(buf);
    sendPacket(pk);
    sendChunkTiles(cx, cz);
    int64_t hash = ((int64_t)cx << 32) | ((uint32_t)cz);
    std::lock_guard<std::mutex> lk(chunks_mtx_);
    used_chunks_.insert(hash);
}} // namespace cursestone
