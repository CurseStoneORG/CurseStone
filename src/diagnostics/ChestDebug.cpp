/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | |  __/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "diagnostics/ChestDebug.h"

#include "Player.h"
#include "nbt/NBT.h"
#include "nbt/tag/CompoundTag.h"
#include "utils/CRaii.h"
#include "utils/MainLogger.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <thread>

namespace cursestone::diagnostics {
namespace {

constexpr uint8_t kPidBlockEntityData = 0x32;
constexpr uint8_t kPidFullChunkData = 0x34;
constexpr size_t kPayloadBlockIds = 32768;  // 16x16x128
constexpr size_t kPayloadBlockData = 16384; // nibbles
constexpr size_t kPayloadScanMin = kPayloadBlockIds + kPayloadBlockData;
constexpr size_t kHexHeadBytes = 48;
constexpr size_t kMaxCellsPerChunk = 16;

std::string traceIdOf(uint64_t id) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%06" PRIu64, id);
    return buf;
}

std::string traceId(const ChestDebug::TileTrace& t) {
    return traceIdOf(t.id);
}

std::string hexPreview(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(kHexHeadBytes * 3);
    const size_t n = len < kHexHeadBytes ? len : kHexHeadBytes;
    char buf[8];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(buf, sizeof(buf), "%02x", data[i]);
        out += buf;
        if (i + 1 < n)
            out += ' ';
    }
    if (len > kHexHeadBytes)
        out += " ...";
    return out;
}

int32_t readBEi32(const uint8_t* p) {
    return static_cast<int32_t>((static_cast<uint32_t>(p[0]) << 24) |
                                (static_cast<uint32_t>(p[1]) << 16) |
                                (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]));
}

// Block metadata nibble for a cell index in the 0.15 ORDER_COLUMNS payload.
uint8_t payloadMeta(const uint8_t* payload, size_t idx) {
    const size_t mi = kPayloadBlockIds + (idx >> 1);
    return (idx & 1) ? static_cast<uint8_t>((payload[mi] >> 4) & 0xF)
                     : static_cast<uint8_t>(payload[mi] & 0xF);
}

} // namespace

size_t ChestDebug::PosKeyHash::operator()(const PosKey& k) const {
    uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(k.x)) * 0x9E3779B97F4A7C15ull;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(k.y)) * 0xC2B2AE3D27D4EB4Full;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(k.z)) * 0x165667B19E3779F9ull;
    h ^= h >> 29;
    return static_cast<size_t>(h);
}

size_t ChestDebug::ChunkKeyHash::operator()(const ChunkKey& k) const {
    uint64_t h = static_cast<uint64_t>(static_cast<uint32_t>(k.cx)) * 0x9E3779B97F4A7C15ull;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(k.cz)) * 0xC2B2AE3D27D4EB4Full;
    h ^= h >> 27;
    return static_cast<size_t>(h);
}

bool ChestDebug::enabled() {
    return get().enabled_.load(std::memory_order_relaxed);
}

void ChestDebug::setEnabled(bool on) {
    ChestDebug& dbg = get();
    const bool was = dbg.enabled_.exchange(on, std::memory_order_relaxed);
    if (was == on)
        return;
    {
        std::lock_guard<std::mutex> lk(dbg.mtx_);
        dbg.tiles_.clear();
        dbg.tileOrder_.clear();
        dbg.chunks_.clear();
        dbg.chunkOrder_.clear();
        dbg.chunkTiles_.clear();
        dbg.findings_.clear();
    }
    utils::MainLogger::get().info(on ? "[ChestDebug] ENABLED - tracing chest generation, tile "
                                       "registry, wire and interaction from now on"
                                     : "[ChestDebug] disabled (traces cleared)");
}

void ChestDebug::clear() {
    ChestDebug& dbg = get();
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    dbg.tiles_.clear();
    dbg.tileOrder_.clear();
    dbg.chunks_.clear();
    dbg.chunkOrder_.clear();
    dbg.chunkTiles_.clear();
    dbg.findings_.clear();
}

ChestDebug& ChestDebug::get() {
    static ChestDebug instance;
    return instance;
}

// --- registry helpers ---

ChestDebug::TileTrace& ChestDebug::findOrCreate(const PosKey& key) {
    auto it = tiles_.find(key);
    if (it != tiles_.end())
        return it->second;
    TileTrace t;
    t.id = nextId_.fetch_add(1, std::memory_order_relaxed);
    t.pos = key;
    t.chunk = ChunkKey{key.x >> 4, key.z >> 4};
    auto [ins, ok] = tiles_.emplace(key, std::move(t));
    (void)ok;
    tileOrder_.push_back(key);
    chunkTiles_[ins->second.chunk].push_back(key);
    while (tiles_.size() > kMaxTiles) {
        const PosKey oldest = tileOrder_.front();
        tileOrder_.pop_front();
        auto tit = tiles_.find(oldest);
        if (tit != tiles_.end()) {
            auto& bucket = chunkTiles_[tit->second.chunk];
            for (size_t i = 0; i < bucket.size(); ++i) {
                if (bucket[i] == oldest) {
                    bucket[i] = bucket.back();
                    bucket.pop_back();
                    break;
                }
            }
            tiles_.erase(tit);
        }
    }
    return ins->second;
}

ChestDebug::TileTrace* ChestDebug::find(const PosKey& key) {
    auto it = tiles_.find(key);
    return it == tiles_.end() ? nullptr : &it->second;
}

void ChestDebug::addLog(TileTrace& t, std::string line) {
    if (t.log.size() >= TileTrace::kMaxLog)
        t.log.erase(t.log.begin());
    t.log.push_back(std::move(line));
}

void ChestDebug::addFinding(const TileTrace& t, const std::string& line) {
    std::string entry = traceId(t) + " " + line;
    if (findings_.size() >= kMaxFindings)
        findings_.pop_front();
    findings_.push_back(entry);
    ++counts_.findings;
    utils::MainLogger::get().info("[ChestDebug] [TRACE " + traceId(t) + "] " + line);
}

void ChestDebug::evaluate(TileTrace& t) {
    // Client liveness: any inbound client packet after the tile flush is
    // the only transport-side proof the session was still answering.
    if (t.tileFlushed && !t.clientAliveAfterSent &&
        get().inboundPackets_.load(std::memory_order_relaxed) > t.inboundAtSent)
        t.clientAliveAfterSent = true;
    uint32_t newBits = 0;
    auto flag = [&](uint32_t bit, const char* line) {
        if (!(t.reported & bit)) {
            t.reported |= bit;
            newBits |= bit;
            addFinding(t, line);
        }
    };
    if (t.removed && t.removedWhy != "block-break") {
        std::string msg = "FAILURE: CHEST TILE REMOVED WITHOUT A PLAYER BREAK (";
        msg += t.removedWhy + ")";
        flag(kRemovedWithoutBreak, msg.c_str());
    }
    if (t.chunkBuilt && !t.removed && t.registered && t.payloadFacing == -2)
        flag(kPayloadNoBlock,
             "FAILURE: CHUNK PAYLOAD HAS NO BLOCK 54 AT THE TILE CELL — the 0.15 client "
             "cannot bind a block actor without its block");
    // tilesDelivered gates this verdict on a COMPLETED tile pass: the
    // batch flush that carries the chunk drains inline inside
    // sendPacket(), before the tile pass that always follows it in the
    // same call stack, so judging at flush time reported every streamed
    // chest as broken while its tile was still being built.
    if (t.chunkFlushed && t.tilesDelivered && !t.tileBuilt && !t.removed &&
        (t.registered || t.generated || t.payloadFacing >= 0))
        flag(kTileNotTransmitted,
             "FAILURE: CHEST TILE NOT TRANSMITTED — the chunk's tile pass completed without "
             "building a BlockEntityData for this chest, so the client holds no block actor");
    if (t.tileBuilt && t.chunkBuilt && t.tileSeq < t.chunkSeq)
        flag(kWireOrder,
             "FAILURE: TILE NBT LEFT THE SERVER BEFORE ITS CHUNK (seq order) — the 0.15 "
             "client wipes a chunk's block actors when the chunk payload arrives");
    if (!t.nbtProblems.empty()) {
        std::string msg = "FAILURE: WIRE NBT SHAPE MISMATCH — " + t.nbtProblems.front();
        flag(kNbtShape, msg.c_str());
    }
    if (t.openCreatedFresh)
        flag(kOpenFreshTile,
             "FAILURE: OPENING CREATED A FRESH TILE — the registry had no TileChest at tap "
             "time, so generation's tile never reached the registry");
    if (t.openCancelled)
        flag(kOpenCancelled, "WARN: InventoryOpenEvent cancelled by a plugin");
    if (t.savedItems >= 0 && t.loadedItems >= 0 && t.savedItems != t.loadedItems)
        flag(kPersistMismatch, "WARN: persistence item count changed between save and load");
}

// --- generation / lifecycle hooks ---

void ChestDebug::onChestGenerated(int x, int y, int z, int facing, const char* lootTable,
                                  size_t blobLen) {
    if (!enabled())
        return;
    ChestDebug& dbg = get();
    char buf[256];
    std::snprintf(buf, sizeof(buf), "GENERATED facing=%d blob=%zuB loot=%s thread=%zx", facing,
                  blobLen, lootTable ? lootTable : "?",
                  std::hash<std::thread::id>{}(std::this_thread::get_id()));
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    TileTrace& t = dbg.findOrCreate(PosKey{x, y, z});
    t.generated = true;
    t.origin = lootTable ? lootTable : "";
    t.facing = facing;
    t.blobLen = blobLen;
    t.genThread = std::hash<std::thread::id>{}(std::this_thread::get_id());
    dbg.addLog(t, buf);
    ++dbg.counts_.generated;
    utils::MainLogger::get().info("[ChestDebug] [TRACE " + traceId(t) + "] " + buf);
}

void ChestDebug::onTileSeam(int x, int y, int z, const char* seam, const char* verdict) {
    if (!enabled())
        return;
    ChestDebug& dbg = get();
    char buf[160];
    std::snprintf(buf, sizeof(buf), "SEAM %s=%s", seam ? seam : "?", verdict ? verdict : "?");
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    TileTrace& t = dbg.findOrCreate(PosKey{x, y, z});
    if (t.seams.size() < TileTrace::kMaxSeams)
        t.seams.emplace_back(buf);
    dbg.addLog(t, buf);
    ++dbg.counts_.seams;
}

void ChestDebug::onTileRegistered(int x, int y, int z, const char* how) {
    if (!enabled())
        return;
    ChestDebug& dbg = get();
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    TileTrace& t = dbg.findOrCreate(PosKey{x, y, z});
    if (!t.registered) {
        t.registered = true;
        t.registeredHow = how ? how : "?";
        dbg.addLog(t, std::string("REGISTERED (") + t.registeredHow + ")");
        ++dbg.counts_.registered;
        utils::MainLogger::get().info("[ChestDebug] [TRACE " + traceId(t) + "] REGISTERED at " +
                                      std::to_string(x) + "," + std::to_string(y) + "," +
                                      std::to_string(z) + " (" + t.registeredHow + ")");
    }
    dbg.evaluate(t);
}

void ChestDebug::onTileRemoved(int x, int y, int z, const char* why) {
    if (!enabled())
        return;
    ChestDebug& dbg = get();
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    TileTrace& t = dbg.findOrCreate(PosKey{x, y, z});
    t.removed = true;
    t.removedWhy = why ? why : "?";
    dbg.addLog(t, std::string("REMOVED (") + t.removedWhy + ")");
    ++dbg.counts_.removed;
    dbg.evaluate(t);
}

void ChestDebug::onBlockHealed(int x, int y, int z, int facing) {
    if (!enabled())
        return;
    ChestDebug& dbg = get();
    char buf[96];
    std::snprintf(buf, sizeof(buf), "BLOCK HEALED back to 54 facing=%d", facing);
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    TileTrace& t = dbg.findOrCreate(PosKey{x, y, z});
    t.healed = true;
    t.healedFacing = facing;
    dbg.addLog(t, buf);
    ++dbg.counts_.healed;
}

// --- wire hooks ---

void ChestDebug::onChunkBuilt(Player& viewer, int cx, int cz, const uint8_t* payload, size_t len) {
    (void)viewer;
    if (!enabled() || !payload || len == 0)
        return;
    ChestDebug& dbg = get();
    const uint64_t seq = dbg.nextSeq_.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    const ChunkKey ck{cx, cz};
    ChunkWire& w = dbg.chunks_[ck];
    if (!w.built) {
        w.built = true;
        w.seq = seq;
        w.payloadLen = len;
        dbg.chunkOrder_.push_back(ck);
        while (dbg.chunks_.size() > kMaxChunks) {
            const ChunkKey oldest = dbg.chunkOrder_.front();
            dbg.chunkOrder_.pop_front();
            dbg.chunks_.erase(oldest);
            dbg.chunkTiles_.erase(oldest);
        }
    }
    ++dbg.counts_.chunkBuilt;

    // Scan the wire payload for chest cells: this is what the CLIENT's block
    // grid will hold, so a chest tile whose cell is not 54 here can never
    // bind its actor.
    int cells = 0;
    const bool scannable = len >= kPayloadScanMin;
    if (scannable) {
        for (size_t idx = 0; idx < kPayloadBlockIds && cells < kMaxCellsPerChunk; ++idx) {
            if (payload[idx] != 54)
                continue;
            const int lx = static_cast<int>(idx >> 11);
            const int lz = static_cast<int>((idx >> 7) & 15);
            const int y = static_cast<int>(idx & 127);
            const int facing = payloadMeta(payload, idx);
            const int wx = cx * 16 + lx;
            const int wz = cz * 16 + lz;
            TileTrace& t = dbg.findOrCreate(PosKey{wx, y, wz});
            t.chunkBuilt = true;
            t.chunkSeq = w.seq;
            t.payloadLen = len;
            t.payloadFacing = facing;
            char buf[128];
            std::snprintf(buf, sizeof(buf), "CHUNK BUILT seq=%" PRIu64 " cell id=54 facing=%d",
                          seq, facing);
            dbg.addLog(t, buf);
            ++cells;
        }
    }
    for (const PosKey& key : dbg.chunkTiles_[ck]) {
        TileTrace* t = dbg.find(key);
        if (!t)
            continue;
        if (!t->chunkBuilt) {
            t->chunkBuilt = true;
            t->chunkSeq = seq;
            t->payloadLen = len;
            if (!scannable)
                t->payloadFacing = -3; // unscannable payload: skip the cell check
        }
        dbg.evaluate(*t);
    }
}

void ChestDebug::onTileBuilt(Player& sender, int x, int y, int z, const uint8_t* nbt, size_t len,
                             const char* recipients) {
    if (!enabled() || !nbt || len == 0)
        return;
    ChestDebug& dbg = get();
    const uint64_t seq = dbg.nextSeq_.fetch_add(1, std::memory_order_relaxed);

    // Re-parse the exact bytes that are about to be queued and validate the
    // era spawn-compound shape against the packet header coordinates.
    TileTrace scratch;
    std::string fields = "?";
    std::vector<std::string> problems;
    {
        utils::TagGuard root(cursestone::nbt::read(nbt, len, /*little_endian=*/true));
        if (!root || root->type != cursestone::nbt::TAG_COMPOUND) {
            problems.emplace_back("NBT did not parse as a compound");
        } else {
            const auto* c = static_cast<const cursestone::nbt::CompoundTag*>(root.get());
            const char* id = cursestone::nbt::get_string(c, "id", "");
            const int nx = cursestone::nbt::get_int(c, "x", 0);
            const int ny = cursestone::nbt::get_int(c, "y", 0);
            const int nz = cursestone::nbt::get_int(c, "z", 0);
            const bool paired = cursestone::nbt::get(c, "pairx") != nullptr;
            const bool named = cursestone::nbt::get(c, "CustomName") != nullptr;
            fields = "id=\"" + std::string(id) + "\" x=" + std::to_string(nx) +
                     " y=" + std::to_string(ny) + " z=" + std::to_string(nz) +
                     (paired ? " +pair" : "") + (named ? " +CustomName" : "");
            if (std::strcmp(id, "Chest") != 0)
                return; // sign/furnace tails: not a chest trace
            if (nx != x || ny != y || nz != z)
                problems.emplace_back("NBT coords (" + std::to_string(nx) + "," +
                                      std::to_string(ny) + "," + std::to_string(nz) +
                                      ") do not match the packet header (" + std::to_string(x) +
                                      "," + std::to_string(y) + "," + std::to_string(z) + ")");
            if (cursestone::nbt::get(c, "Items") != nullptr)
                problems.emplace_back("disk layout on the wire (Items list — the era spawn "
                                      "compound carries no Items)");
        }
    }

    std::lock_guard<std::mutex> lk(dbg.mtx_);
    TileTrace& t = dbg.findOrCreate(PosKey{x, y, z});
    t.tileBuilt = true;
    t.tileSeq = seq;
    t.nbtLen = len;
    t.nbtFields = fields;
    t.nbtHex = hexPreview(nbt, len);
    t.nbtProblems = std::move(problems);
    t.tileSentPreSpawn = !sender.spawned();
    t.inboundAtSent = dbg.inboundPackets_.load(std::memory_order_relaxed);
    if (const char* who = recipients ? recipients : nullptr; who && *who) {
        if (std::find(t.recipients.begin(), t.recipients.end(), who) == t.recipients.end()) {
            if (t.recipients.size() < TileTrace::kMaxRecipients)
                t.recipients.emplace_back(who);
        }
    }
    (void)sender;
    char buf[192];
    std::snprintf(buf, sizeof(buf), "TILE BUILT seq=%" PRIu64 " nbt=%zuB shape=%s", seq, len,
                  t.nbtProblems.empty() ? "OK" : "MISMATCH");
    dbg.addLog(t, buf);
    ++dbg.counts_.tileBuilt;
    utils::MainLogger::get().info("[ChestDebug] [TRACE " + traceId(t) + "] " + buf + " " +
                                  t.nbtFields);
    dbg.evaluate(t);
}

void ChestDebug::onBodyQueued(const char* player, uint8_t pid, const uint8_t* payload,
                              size_t payloadLen) {
    if (!enabled() || !payload)
        return;
    ChestDebug& dbg = get();
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    if (pid == kPidFullChunkData && payloadLen >= 8) {
        const ChunkKey ck{readBEi32(payload), readBEi32(payload + 4)};
        auto it = dbg.chunks_.find(ck);
        if (it != dbg.chunks_.end())
            it->second.queued = true;
        for (const PosKey& key : dbg.chunkTiles_[ck]) {
            if (TileTrace* t = dbg.find(key); t && !t->chunkQueued) {
                t->chunkQueued = true;
                dbg.addLog(*t, std::string("CHUNK QUEUED on ch2 (to ") + (player ? player : "?") +
                                   ")");
                ++dbg.counts_.queued;
            }
        }
    } else if (pid == kPidBlockEntityData && payloadLen >= 12) {
        const PosKey key{readBEi32(payload), readBEi32(payload + 4), readBEi32(payload + 8)};
        if (TileTrace* t = dbg.find(key); t && !t->tileQueued) {
            t->tileQueued = true;
            dbg.addLog(*t, std::string("TILE QUEUED on ch2 (to ") + (player ? player : "?") + ")");
            ++dbg.counts_.queued;
        }
    }
}

void ChestDebug::onBodiesFlushed(const char* player,
                                 const std::vector<std::vector<uint8_t>>& chunkQueue) {
    if (!enabled() || chunkQueue.empty())
        return;
    ChestDebug& dbg = get();
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    const std::string who = player ? player : "?";
    bool touched = false;
    for (const auto& body : chunkQueue) {
        if (body.size() < 9)
            continue;
        const uint8_t pid = body[0];
        const uint8_t* payload = body.data() + 1;
        const size_t payloadLen = body.size() - 1;
        if (pid == kPidFullChunkData && payloadLen >= 8) {
            const ChunkKey ck{readBEi32(payload), readBEi32(payload + 4)};
            auto it = dbg.chunks_.find(ck);
            if (it != dbg.chunks_.end())
                it->second.flushed = true;
            for (const PosKey& key : dbg.chunkTiles_[ck]) {
                if (TileTrace* t = dbg.find(key); t && !t->chunkFlushed) {
                    t->chunkFlushed = true;
                    dbg.addLog(*t, "CHUNK SENT to transport (ch2)");
                    dbg.evaluate(*t);
                    touched = true;
                }
            }
        } else if (pid == kPidBlockEntityData && payloadLen >= 12) {
            const PosKey key{readBEi32(payload), readBEi32(payload + 4), readBEi32(payload + 8)};
            if (TileTrace* t = dbg.find(key); t && !t->tileFlushed) {
                t->tileFlushed = true;
                t->inboundAtSent = dbg.inboundPackets_.load(std::memory_order_relaxed);
                dbg.addLog(*t, "TILE SENT to transport (ch2, to " + who + ")");
                dbg.evaluate(*t);
                touched = true;
            }
        }
    }
    if (touched)
        ++dbg.counts_.flushed;
}

void ChestDebug::onChunkTilesDelivered(int cx, int cz) {
    if (!enabled())
        return;
    ChestDebug& dbg = get();
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    const ChunkKey ck{cx, cz};
    auto bit = dbg.chunkTiles_.find(ck);
    if (bit == dbg.chunkTiles_.end())
        return; // No chest trace lives in this chunk.
    for (const PosKey& key : bit->second) {
        TileTrace* t = dbg.find(key);
        if (!t)
            continue;
        t->tilesDelivered = true;
        dbg.evaluate(*t);
    }
}

// --- interaction hooks ---

void ChestDebug::onOpenRequest(Player& p, int x, int y, int z) {
    if (!enabled())
        return;
    ChestDebug& dbg = get();
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    TileTrace& t = dbg.findOrCreate(PosKey{x, y, z});
    t.openRequested = true;
    t.lastPlayer = p.getName();
    dbg.addLog(t, "OPEN REQUEST by " + t.lastPlayer +
                      " (client asked; tile lookup follows)");
    // No counter here: onOpenAttempt counts the actual open processing,
    // and the request/attempt pair would double-count one interaction.
}

void ChestDebug::onChestTapped(Player& p, int x, int y, int z, int serverBlockId,
                               bool interactCancelled) {
    if (!enabled())
        return;
    ChestDebug& dbg = get();
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    TileTrace& t = dbg.findOrCreate(PosKey{x, y, z});
    t.tapped = true;
    t.tapBlockId = serverBlockId;
    t.tapCancelled = interactCancelled;
    t.lastPlayer = p.getName();
    dbg.addLog(t, "TAP by " + t.lastPlayer + ": server block=" + std::to_string(serverBlockId) +
                      (interactCancelled ? " (interact event CANCELLED)" : ""));
    ++dbg.counts_.tapped;
}

void ChestDebug::onOpenAttempt(Player& p, int x, int y, int z, bool tileFound, bool tileCreatedFresh,
                               bool paired) {
    if (!enabled())
        return;
    ChestDebug& dbg = get();
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    TileTrace& t = dbg.findOrCreate(PosKey{x, y, z});
    t.openAttempted = true;
    t.openTileFound = tileFound;
    t.openCreatedFresh = tileCreatedFresh;
    t.lastPlayer = p.getName();
    dbg.addLog(t, "OPEN ATTEMPT by " + t.lastPlayer +
                      (tileFound ? ": tile found" : ": NO TILE — created fresh") +
                      (paired ? " (paired)" : ""));
    ++dbg.counts_.openAttempted;
    dbg.evaluate(t);
}

void ChestDebug::onOpenCancelled(Player& p, int x, int y, int z) {
    if (!enabled())
        return;
    ChestDebug& dbg = get();
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    TileTrace& t = dbg.findOrCreate(PosKey{x, y, z});
    t.openCancelled = true;
    t.lastPlayer = p.getName();
    dbg.addLog(t, "OPEN CANCELLED (InventoryOpenEvent) for " + t.lastPlayer);
    ++dbg.counts_.openCancelled;
    dbg.evaluate(t);
}

void ChestDebug::onOpenDone(Player& p, int x, int y, int z, int slots) {
    if (!enabled())
        return;
    ChestDebug& dbg = get();
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    TileTrace& t = dbg.findOrCreate(PosKey{x, y, z});
    t.openDone = true;
    t.openSlots = slots;
    t.lastPlayer = p.getName();
    char buf[96];
    std::snprintf(buf, sizeof(buf), "OPENED by %s: slots=%d (ContainerOpen + SetContent sent)",
                  t.lastPlayer.c_str(), slots);
    dbg.addLog(t, buf);
    ++dbg.counts_.openDone;
}

void ChestDebug::onChestBroken(Player& p, int x, int y, int z, int lootStacks, bool tileFound,
                               bool hadPair) {
    if (!enabled())
        return;
    ChestDebug& dbg = get();
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    TileTrace& t = dbg.findOrCreate(PosKey{x, y, z});
    t.broken = true;
    t.lootStacks = lootStacks;
    t.lastPlayer = p.getName();
    dbg.addLog(t, "BROKEN by " + t.lastPlayer + ": loot stacks=" + std::to_string(lootStacks) +
                      (tileFound ? "" : " (NO TILE at break)") + (hadPair ? " (was paired)" : ""));
    ++dbg.counts_.broken;
}

// --- persistence hooks ---

void ChestDebug::onTileResync(const char* player, int x, int y, int z) {
    if (!enabled())
        return;
    ChestDebug& dbg = get();
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    TileTrace& t = dbg.findOrCreate(PosKey{x, y, z});
    t.resynced = true;
    dbg.addLog(t, std::string("TILE RESYNC (post-spawn) to ") + (player ? player : "?"));
}

void ChestDebug::onClientInbound(const char* player) {
    (void)player;
    if (!enabled())
        return;
    get().inboundPackets_.fetch_add(1, std::memory_order_relaxed);
}

void ChestDebug::onTileSaved(int x, int y, int z, int items) {
    if (!enabled())
        return;
    ChestDebug& dbg = get();
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    TileTrace& t = dbg.findOrCreate(PosKey{x, y, z});
    t.savedItems = items;
    dbg.addLog(t, "SAVED Items=" + std::to_string(items));
    ++dbg.counts_.saved;
    dbg.evaluate(t);
}

void ChestDebug::onTileLoaded(int x, int y, int z, int items) {
    if (!enabled())
        return;
    ChestDebug& dbg = get();
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    TileTrace& t = dbg.findOrCreate(PosKey{x, y, z});
    t.loadedItems = items;
    dbg.addLog(t, "LOADED Items=" + std::to_string(items));
    ++dbg.counts_.loaded;
    dbg.evaluate(t);
}

// --- reports ---

namespace {

void appendResult(const ChestDebug::TileTrace& t, std::vector<std::string>& out) {
    out.emplace_back("[ChestDebug] RESULT:");
    std::vector<std::string> problems;
    if (t.removed && t.removedWhy != "block-break")
        problems.push_back("FAILURE: CHEST TILE REMOVED WITHOUT A PLAYER BREAK (" + t.removedWhy +
                           ")");
    if (t.chunkBuilt && !t.removed && t.registered && t.payloadFacing == -2)
        problems.push_back("FAILURE: CHUNK PAYLOAD HAS NO BLOCK 54 AT THE TILE CELL");
    if (t.chunkFlushed && t.tilesDelivered && !t.tileBuilt && !t.removed &&
        (t.registered || t.generated || t.payloadFacing >= 0))
        problems.push_back("FAILURE: CHEST TILE NOT TRANSMITTED (the chunk's tile pass completed "
                           "without a BlockEntityData for this chest)");
    if (t.tileBuilt && t.chunkBuilt && t.tileSeq < t.chunkSeq)
        problems.push_back("FAILURE: TILE NBT LEFT BEFORE ITS CHUNK (seq " +
                           std::to_string(t.tileSeq) + " < " + std::to_string(t.chunkSeq) + ")");
    if (!t.nbtProblems.empty())
        for (const auto& p : t.nbtProblems)
            problems.push_back("FAILURE: WIRE NBT SHAPE MISMATCH — " + p);
    if (t.openCreatedFresh)
        problems.push_back("FAILURE: OPENING CREATED A FRESH TILE (registry was empty)");
    if (t.openCancelled)
        problems.push_back("WARN: InventoryOpenEvent cancelled by a plugin");
    if (t.savedItems >= 0 && t.loadedItems >= 0 && t.savedItems != t.loadedItems)
        problems.push_back("WARN: persistence item count changed between save and load");

    const bool allGreen =
        problems.empty() && t.tileFlushed && t.chunkFlushed && t.registered && t.payloadFacing >= 0;
    const bool noFailures = problems.empty();
    if (allGreen && t.openDone) {
        out.emplace_back("[ChestDebug]  SERVER CHAIN COMPLETE — CLIENT_RENDER_CONFIRMED: UNKNOWN");
        out.emplace_back("[ChestDebug]  (the protocol carries no render confirmation; if the");
        out.emplace_back("[ChestDebug]   client still draws nothing with this trace green,");
        out.emplace_back("[ChestDebug]   capture the network traffic and diff it against the");
        out.emplace_back("[ChestDebug]   NBT hex head above)");
    } else if (noFailures) {
        out.emplace_back("[ChestDebug]  NO FAILURES SO FAR — CLIENT_RENDER_CONFIRMED: UNKNOWN");
        if (!t.openDone && !t.broken)
            out.emplace_back("[ChestDebug]  (tap the chest in game to complete the trace)");
    } else {
        for (const auto& p : problems)
            out.emplace_back("[ChestDebug]  " + p);
    }

    // TRANSPORT context — what SENT can and cannot prove. A live session
    // after the flush means the reliability layer held delivery; a silent
    // one means the bytes never reached an answering client.
    out.emplace_back("[ChestDebug]  TRANSPORT: tile handed to RELIABLE_ORDERED ch2; client "
                     "inbound traffic after SENT: " +
                     std::string(t.clientAliveAfterSent ? "YES" : "NO/unknown") +
                     std::string(t.tileSentPreSpawn
                                     ? "  | tile wire context: PRE-SPAWN (the 0.15 loading "
                                       "screen can drop tile NBT — post-spawn resync: "
                                     : "  | tile wire context: POST-SPAWN (resync: ") +
                     std::string(t.resynced ? "DONE" : "none") + ")");
}

} // namespace

std::vector<std::string> ChestDebug::statusLines() {
    ChestDebug& dbg = get();
    std::vector<std::string> out;
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "[ChestDebug] enabled=%s traces=%zu chunks=%zu findings=%zu",
                  dbg.enabled_.load(std::memory_order_relaxed) ? "YES" : "NO", dbg.tiles_.size(),
                  dbg.chunks_.size(), dbg.findings_.size());
    out.emplace_back(buf);
    const Counts& c = dbg.counts_;
    std::snprintf(buf, sizeof(buf),
                  "[ChestDebug] counters: generated=%" PRIu64 " registered=%" PRIu64
                  " tileBuilt=%" PRIu64 " flushed=%" PRIu64 " opened=%" PRIu64 "/%" PRIu64
                  " broken=%" PRIu64,
                  c.generated, c.registered, c.tileBuilt, c.flushed, c.openDone, c.openAttempted,
                  c.broken);
    out.emplace_back(buf);
    out.emplace_back("[ChestDebug] recent findings (oldest first, empty = none):");
    for (const auto& f : dbg.findings_)
        out.emplace_back("[ChestDebug]  " + f);
    if (dbg.findings_.empty())
        out.emplace_back("[ChestDebug]  (none)");
    out.emplace_back("[ChestDebug] commands: on | off | status | inspect <x> <y> <z> | trace "
                     "<player> | clear");
    return out;
}

std::vector<std::string> ChestDebug::reportLines(int x, int y, int z) {
    ChestDebug& dbg = get();
    std::vector<std::string> out;
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    const TileTrace* t = dbg.find(PosKey{x, y, z});
    if (!t) {
        out.emplace_back("[ChestDebug] ==========================================");
        char buf[128];
        std::snprintf(buf, sizeof(buf), "[ChestDebug] NO TRACE for x=%d y=%d z=%d", x, y, z);
        out.emplace_back(buf);
        out.emplace_back("[ChestDebug] traces are born while the mode is ON: enable it, then");
        out.emplace_back("[ChestDebug] re-enter the area (or reconnect) so the chest re-streams.");
        out.emplace_back("[ChestDebug] ==========================================");
        return out;
    }
    const TileTrace& tr = *t;
    out.emplace_back("[ChestDebug] ==========================================");
    out.emplace_back("[ChestDebug] CHEST TRACE ID: " + traceId(tr) + "  x=" +
                     std::to_string(tr.pos.x) + " y=" + std::to_string(tr.pos.y) + " z=" +
                     std::to_string(tr.pos.z) + "  chunk=" + std::to_string(tr.chunk.cx) + "," +
                     std::to_string(tr.chunk.cz));
    out.emplace_back("[ChestDebug] ------------------------------------------");

    out.emplace_back("[ChestDebug] GENERATOR:");
    out.emplace_back("[ChestDebug]  generated=" + std::string(tr.generated ? "YES" : "NO") +
                     (tr.generated ? "  facing=" + std::to_string(tr.facing) +
                                         "  blob=" + std::to_string(tr.blobLen) + "B" +
                                         ("  origin=" + (tr.origin.empty() ? "?" : tr.origin))
                                   : "") +
                     (tr.generated ? "  gen-thread=" + std::to_string(tr.genThread) : ""));
    if (!tr.seams.empty()) {
        std::string joined;
        for (const auto& s : tr.seams)
            joined += (joined.empty() ? "" : " | ") + s;
        out.emplace_back("[ChestDebug]  seams: " + joined);
    } else {
        out.emplace_back("[ChestDebug]  seams: (none observed)");
    }

    out.emplace_back("[ChestDebug] TILE REGISTRY:");
    out.emplace_back("[ChestDebug]  registered=" + std::string(tr.registered ? "YES" : "NO") +
                     (tr.registered ? " (" + tr.registeredHow + ")" : "") +
                     "  removed=" + std::string(tr.removed ? "YES (" + tr.removedWhy + ")" : "no") +
                     "  healed=" + std::string(tr.healed ? "YES (facing " +
                                                               std::to_string(tr.healedFacing) + ")"
                                                        : "no"));

    out.emplace_back("[ChestDebug] PERSISTENCE:");
    out.emplace_back("[ChestDebug]  saved Items=" +
                     std::string(tr.savedItems < 0 ? "-" : std::to_string(tr.savedItems)) +
                     "  loaded Items=" +
                     std::string(tr.loadedItems < 0 ? "-" : std::to_string(tr.loadedItems)));

    out.emplace_back("[ChestDebug] CLIENT WIRE (SENT = handed to the transport):");
    if (tr.chunkBuilt) {
        out.emplace_back("[ChestDebug]  FullChunkData 0x34: BUILT(seq " + std::to_string(tr.chunkSeq) +
                         ", " + std::to_string(tr.payloadLen) + "B) -> " +
                         std::string(tr.chunkQueued ? "QUEUED " : "") +
                         std::string(tr.chunkFlushed ? "SENT" : "NOT SENT") +
                         "  | cell scan: " +
                         std::string(tr.payloadFacing == -3
                                         ? "payload unscannable"
                                         : tr.payloadFacing >= 0
                                               ? "id=54 facing=" + std::to_string(tr.payloadFacing)
                                               : "NO BLOCK 54 AT CELL"));
    } else {
        out.emplace_back("[ChestDebug]  FullChunkData 0x34: (never built while tracing)");
    }
    if (tr.tileBuilt) {
        out.emplace_back("[ChestDebug]  BlockEntityData 0x32: BUILT(seq " + std::to_string(tr.tileSeq) +
                         ", " + std::to_string(tr.nbtLen) + "B) -> " +
                         std::string(tr.tileQueued ? "QUEUED " : "") +
                         std::string(tr.tileFlushed ? "SENT" : "NOT SENT") +
                         std::string("  | shape=") +
                         std::string(tr.nbtProblems.empty() ? "OK" : "MISMATCH"));
        out.emplace_back("[ChestDebug]  NBT: " + tr.nbtFields);
        out.emplace_back("[ChestDebug]  hex head: " + tr.nbtHex);
        for (const auto& p : tr.nbtProblems)
            out.emplace_back("[ChestDebug]  problem: " + p);
        if (!tr.recipients.empty()) {
            std::string joined;
            for (const auto& r : tr.recipients)
                joined += (joined.empty() ? "" : ", ") + r;
            out.emplace_back("[ChestDebug]  recipients: " + joined);
        }
    } else {
        out.emplace_back("[ChestDebug]  BlockEntityData 0x32: (never built while tracing)");
    }

    out.emplace_back("[ChestDebug] INTERACTION:");
    out.emplace_back("[ChestDebug]  tap=" + std::string(tr.tapped ? "YES" : "no") +
                     (tr.tapped ? " (server block=" + std::to_string(tr.tapBlockId) +
                                      (tr.tapCancelled ? ", interact CANCELLED" : "") + ")"
                                : "") +
                     "  openRequest=" + std::string(tr.openRequested ? "YES" : "no") +
                     "  openAttempt=" + std::string(tr.openAttempted ? "YES" : "no") +
                     (tr.openAttempted
                          ? std::string(" (") +
                                (tr.openTileFound ? "tile found" : "NO tile — fresh") + ")"
                          : "") +
                     "  serverOpened=" +
                     std::string(tr.openDone
                                     ? "YES (ContainerOpen + SetContent SENT, slots " +
                                           std::to_string(tr.openSlots) + ")"
                                     : "no") +
                     "  cancelled=" + std::string(tr.openCancelled ? "YES" : "no"));
    if (tr.openDone)
        out.emplace_back("[ChestDebug]  (serverOpened = the server built and sent the window — "
                         "the 0.15 client renders it only when its block actor exists)");
    out.emplace_back("[ChestDebug]  broken=" + std::string(tr.broken ? "YES" : "no") +
                     (tr.broken ? " (loot stacks=" +
                                      std::string(tr.lootStacks < 0 ? "-" :
                                                                    std::to_string(tr.lootStacks)) +
                                      ")"
                                : ""));

    if (!tr.log.empty()) {
        out.emplace_back("[ChestDebug] EVENTS (oldest first):");
        size_t start = tr.log.size() > 16 ? tr.log.size() - 16 : 0;
        for (size_t i = start; i < tr.log.size(); ++i)
            out.emplace_back("[ChestDebug]  " + tr.log[i]);
    }

    appendResult(tr, out);
    out.emplace_back("[ChestDebug] ==========================================");
    return out;
}

std::vector<std::string> ChestDebug::playerTraceLines(const std::string& player) {
    ChestDebug& dbg = get();
    std::vector<std::string> out;
    std::lock_guard<std::mutex> lk(dbg.mtx_);
    out.emplace_back("[ChestDebug] ==========================================");
    out.emplace_back("[ChestDebug] CHESTS TRACED FOR PLAYER: " + player);
    size_t n = 0;
    for (auto& [key, t] : dbg.tiles_) {
        (void)key;
        const bool related =
            t.lastPlayer == player ||
            std::find(t.recipients.begin(), t.recipients.end(), player) != t.recipients.end();
        if (!related)
            continue;
        ++n;
        std::string line = "[ChestDebug]  TRACE " + traceId(t) + "  x=" + std::to_string(t.pos.x) +
                           " y=" + std::to_string(t.pos.y) + " z=" + std::to_string(t.pos.z) +
                           "  tile=" + (t.tileBuilt ? "SENT" : t.registered ? "REG" : "?") +
                           "  open=" + (t.openDone ? "YES" : t.openCancelled ? "CANCELLED"
                                                                       : t.openAttempted ? "FAIL"
                                                                                         : "-");
        out.emplace_back(line);
        for (const auto& p : t.nbtProblems)
            out.emplace_back("[ChestDebug]    ! " + p);
    }
    if (n == 0) {
        out.emplace_back("[ChestDebug]  (no chest traces name this player — walk past chests with");
        out.emplace_back("[ChestDebug]   the mode ON, or /chestdebug inspect <x> <y> <z> a spot)");
    }
    out.emplace_back("[ChestDebug] ==========================================");
    return out;
}

} // namespace cursestone::diagnostics
