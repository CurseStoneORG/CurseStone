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
// ChestDebug — per-chest trace from generation to the client.
//
// Follows one chest through the four stages where the 0.15 client can lose
// it, and records which stage broke so invisible-chest reports come with a
// diagnosis instead of a guess:
//
//   1. GENERATION  LootChest writes block 54 + the tile blob, every seam the
//                  blob crosses (populate flush, spill, install drains,
//                  reconcile) with its block-match verdict.
//   2. REGISTRY    World register/remove/heal of the TileChest.
//   3. WIRE        FullChunkData 0x34 and BlockEntityData 0x32 for the
//                  chest, each with its BUILT / QUEUED / SENT stage and a
//                  monotonic sequence number so the report can prove the
//                  tile left AFTER its chunk. The tile NBT is re-parsed and
//                  validated against the era spawn-compound shape (id, x/y/z
//                  matching the packet header, no disk layout on the wire)
//                  plus a hex head for byte-level comparison against a
//                  network capture.
//   4. INTERACTION client taps, InventoryOpenEvent, ContainerOpen /
//                  ContainerSetContent, break + loot, save/load items.
//
// Every hook is a single relaxed atomic load when the mode is off; nothing
// allocates on the hot path. Enabled state is the /chestdebug command
// (operator-only). "SENT" always means "handed to the transport layer": the
// protocol carries no render confirmation, and reports say so instead of
// inventing one.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace cursestone {
class Player;
namespace diagnostics {

class ChestDebug {
public:
    // --- mode ---
    static bool enabled();
    static void setEnabled(bool on);
    static void clear();

    // --- generation / tile lifecycle ---
    static void onChestGenerated(int x, int y, int z, int facing, const char* lootTable,
                                 size_t blobLen);
    static void onTileSeam(int x, int y, int z, const char* seam, const char* verdict);
    static void onTileRegistered(int x, int y, int z, const char* how);
    static void onTileRemoved(int x, int y, int z, const char* why);
    static void onBlockHealed(int x, int y, int z, int facing);

    // --- wire (BUILT happens at the encoder, QUEUED at the batch push,
    //     SENT when the drained bodies are handed to the transport layer) ---
    static void onChunkBuilt(Player& viewer, int cx, int cz, const uint8_t* payload, size_t len);
    static void onTileBuilt(Player& sender, int x, int y, int z, const uint8_t* nbt, size_t len,
                            const char* recipients);
    static void onBodyQueued(const char* player, uint8_t pid, const uint8_t* payload,
                             size_t payloadLen);
    static void onBodiesFlushed(const char* player,
                                const std::vector<std::vector<uint8_t>>& chunkQueue);
    // Closed after sendChunkTiles() had its full chance to build this
    // chunk's tile NBTs. The tile-transmission verdict is only meaningful
    // HERE: the batch flush that carries the chunk can drain INLINE inside
    // sendPacket() — before the tile pass that always follows it in the
    // same call stack — so judging "tile not transmitted" at flush time
    // would report every streamed chest as broken.
    static void onChunkTilesDelivered(int cx, int cz);

    // --- interaction ---
    static void onChestTapped(Player& p, int x, int y, int z, int serverBlockId,
                              bool interactCancelled);
    // The client ASKED to open (tap reached the server, before any tile
    // lookup). Distinct from onOpenDone, which is the server-side
    // container opening — neither implies the client rendered a window.
    static void onOpenRequest(Player& p, int x, int y, int z);
    static void onOpenAttempt(Player& p, int x, int y, int z, bool tileFound, bool tileCreatedFresh,
                              bool paired);
    static void onOpenCancelled(Player& p, int x, int y, int z);
    static void onOpenDone(Player& p, int x, int y, int z, int slots);
    static void onChestBroken(Player& p, int x, int y, int z, int lootStacks, bool tileFound,
                              bool hadPair);

    // --- persistence ---
    static void onTileSaved(int x, int y, int z, int items);
    static void onTileLoaded(int x, int y, int z, int items);

    // --- post-spawn tile resync + client liveness ---
    // The resync is what heals the tiles the client dropped while its
    // loading screen was up: 0.15 block actors bind only from in-world
    // traffic, and the era gate (tiles only for spawned viewers) exists
    // because the loading screen eats them.
    static void onTileResync(const char* player, int x, int y, int z);
    // One relaxed atomic increment per inbound client packet while the
    // mode is on. "Traffic kept flowing after SENT" is the only
    // transport-side fact the server can verify — a dead session means
    // transport loss, a live one means the reliability layer held delivery.
    static void onClientInbound(const char* player);

    // --- reports (for the /chestdebug command) ---
    static std::vector<std::string> statusLines();
    static std::vector<std::string> reportLines(int x, int y, int z);
    static std::vector<std::string> playerTraceLines(const std::string& player);

    // Trace data structures are plain records the report renderers read;
    // all mutation goes through the hooks above.
    struct PosKey {
        int32_t x = 0, y = 0, z = 0;
        bool operator==(const PosKey& o) const {
            return x == o.x && y == o.y && z == o.z;
        }
    };
    struct PosKeyHash {
        size_t operator()(const PosKey& k) const;
    };
    struct ChunkKey {
        int32_t cx = 0, cz = 0;
        bool operator==(const ChunkKey& o) const {
            return cx == o.cx && cz == o.cz;
        }
    };
    struct ChunkKeyHash {
        size_t operator()(const ChunkKey& k) const;
    };

    // One chest's full observable state. `sent` stages are transport-layer
    // facts, never client render confirmations.
    struct TileTrace {
        uint64_t id = 0;
        PosKey pos{};
        ChunkKey chunk{};

        // generation
        bool generated = false;
        std::string origin;
        int facing = -1;
        size_t blobLen = 0;
        size_t genThread = 0;
        std::vector<std::string> seams;

        // registry
        bool registered = false;
        std::string registeredHow;
        bool removed = false;
        std::string removedWhy;
        bool healed = false;
        int healedFacing = -1;

        // persistence
        int savedItems = -1;
        int loadedItems = -1;

        // wire
        bool chunkBuilt = false, chunkQueued = false, chunkFlushed = false;
        uint64_t chunkSeq = 0;
        size_t payloadLen = 0;
        int payloadFacing = -2; // facing seen in the chunk payload at this cell; -2 = not seen
        bool tileBuilt = false, tileQueued = false, tileFlushed = false;
        uint64_t tileSeq = 0;
        size_t nbtLen = 0;
        std::string nbtFields;
        std::string nbtHex;
        std::vector<std::string> nbtProblems;
        std::vector<std::string> recipients;
        // Set when a full chunk tile pass finished for this chest's chunk
        // (sendChunkTiles ran to completion) — the gate that keeps the
        // tile-transmission verdict off until the pass could actually
        // build the NBT.
        bool tilesDelivered = false;
        bool tileSentPreSpawn = false;
        bool resynced = false;
        // Client-liveness probe: inbound packet counter snapshot taken when
        // the tile flush fired; a higher counter later proves the client
        // kept answering after the tile left the server.
        uint64_t inboundAtSent = 0;
        bool clientAliveAfterSent = false;

        // interaction
        bool tapped = false;
        int tapBlockId = -1;
        bool tapCancelled = false;
        bool openRequested = false;
        bool openAttempted = false, openTileFound = false, openCreatedFresh = false;
        bool openCancelled = false, openDone = false;
        int openSlots = -1;
        std::string lastPlayer;
        bool broken = false;
        int lootStacks = -1;

        // ordered log + failure dedup
        std::vector<std::string> log;
        uint32_t reported = 0; // bitmask of already-reported failure checks

        static constexpr size_t kMaxSeams = 12;
        static constexpr size_t kMaxLog = 48;
        static constexpr size_t kMaxRecipients = 8;
        static constexpr size_t kMaxProblems = 6;
    };

    struct ChunkWire {
        bool built = false, queued = false, flushed = false;
        uint64_t seq = 0;
        size_t payloadLen = 0;
    };

    enum CheckBit : uint32_t {
        kNeverRegistered = 1u << 0,
        kRemovedWithoutBreak = 1u << 1,
        kPayloadNoBlock = 1u << 2,
        kTileNotTransmitted = 1u << 3,
        kWireOrder = 1u << 4,
        kNbtShape = 1u << 5,
        kQueuedNeverFlushed = 1u << 6,
        kOpenFreshTile = 1u << 7,
        kOpenCancelled = 1u << 8,
        kPersistMismatch = 1u << 9,
    };

    static constexpr size_t kMaxTiles = 256;
    static constexpr size_t kMaxChunks = 512;
    static constexpr size_t kMaxFindings = 24;

private:
    std::atomic<bool> enabled_{false};
    std::atomic<uint64_t> nextSeq_{1};
    std::atomic<uint64_t> nextId_{1};
    std::atomic<uint64_t> inboundPackets_{0};

    mutable std::mutex mtx_;
    std::unordered_map<PosKey, TileTrace, PosKeyHash> tiles_;
    std::deque<PosKey> tileOrder_;
    std::unordered_map<ChunkKey, ChunkWire, ChunkKeyHash> chunks_;
    std::deque<ChunkKey> chunkOrder_;
    std::unordered_map<ChunkKey, std::vector<PosKey>, ChunkKeyHash> chunkTiles_;

    struct Counts {
        uint64_t generated = 0, seams = 0, registered = 0, removed = 0, healed = 0;
        uint64_t chunkBuilt = 0, tileBuilt = 0, queued = 0, flushed = 0;
        uint64_t tapped = 0, openAttempted = 0, openCancelled = 0, openDone = 0, broken = 0;
        uint64_t saved = 0, loaded = 0, findings = 0;
    } counts_;

    std::deque<std::string> findings_; // newest last, bounded

private:
    ChestDebug() = default;
    static ChestDebug& get();
    TileTrace& findOrCreate(const PosKey& key);
    TileTrace* find(const PosKey& key);
    void addLog(TileTrace& t, std::string line);
    void addFinding(const TileTrace& t, const std::string& line);
    void evaluate(TileTrace& t);
};

} // namespace diagnostics
} // namespace cursestone
