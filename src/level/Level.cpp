// SPDX-License-Identifier: LGPL-3.0-or-later
// Level.cpp — implementation. The constructor instantiates a
// LevelProvider via LevelProviderManager and connects it to the
// ChunkManager so flushDirty() writes real bytes to disk.
#include "Level.h"
#include "level/format/Chunk.h"
#include "level/format/LevelProvider.h"
#include "level/format/LevelProviderManager.h"
#include "level/format/ChunkAdapter.h"
#include "level/generator/Generator.h"
#include "level/generator/Normal.h"
#include "level/generator/Flat.h"
#include "level/generator/Nether.h"
#include "level/generator/EmptyGenerator.h"
#include "utils/MainLogger.hpp"
#include "tile/Tile.h"
#include "tile/TileChest.h"
#include "tile/TileFurnace.h"
#include "tile/TileSign.h"
#include "nbt/NBT.h"
#include "nbt/tag/CompoundTag.h"
#include "Player.h"
#include "Server.h"
#include "block/BlockIds.h"
#include "block/BlockFire.h"
#include "network/protocol/LevelEventPacket.h"
#include "network/protocol/UpdateBlockPacket.h"
#include "network/protocol/AddEntityPacket.h"
#include "network/protocol/ExplodePacket.h"
#include "network/protocol/RemoveEntityPacket.h"
#include "scheduler/ServerScheduler.h"
#include "scheduler/Task.h"
#include <cmath>

#include <sys/stat.h>
#include <sys/types.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <cstddef>
#include <sstream>
#include <string>

namespace cursestone::level {

// Creates `path` and every missing parent directory (mkdir -p
// semantics). The previous version of this function was a single
// non-recursive ::mkdir() call — it silently failed with ENOENT
// whenever `path`'s parent didn't already exist (e.g. a brand-new
// dataPath on a fresh deploy, or any multi-level path that hadn't
// been created by something else first), and the error was discarded,
// so a Level would silently end up "in-memory only" with no world
// files ever touching disk and no warning printed. Found while
// verifying tile persistence end-to-end (see Level::save() /
// ensureTilesLoaded()): a save() reported success and correctly
// populated the in-memory chunk's tile_entities, but no file ever
// appeared on disk because ensureDir() had failed upstream on the
// world directory itself.
static void ensureDir(const std::string& path) {
    if (path.empty()) return;
    std::string partial;
    partial.reserve(path.size());
    size_t i = 0;
    // Skip a leading '/' so we don't try to mkdir("") first.
    if (path[0] == '/') { partial += '/'; i = 1; }
    while (i <= path.size()) {
        if (i == path.size() || path[i] == '/') {
            if (!partial.empty() && partial != "/") {
                struct stat st{};
                if (::stat(partial.c_str(), &st) != 0) {
                    ::mkdir(partial.c_str(), 0755);
                }
            }
        }
        if (i < path.size() && path[i] == '/') partial += '/';
        else if (i < path.size()) partial += path[i];
        ++i;
    }
}

Level::Level(LevelConfig cfg) : cfg_(std::move(cfg)) {
    // Spawn is NOT hardcoded here — it comes from the generator's
    // getSpawn() method, mirroring Nukkit where each generator type
    // defines its own spawn (Normal, Flat, Nether all differ).
    // We initialize it to a safe default; the generator overrides it.

    // Determine the world directory path. We use either:
    //   <dataPath>/worlds/<name>   (if dataPath is set), or
    //   worlds/<name>              (relative fallback for legacy code)
    std::string worldsRoot = cfg_.dataPath.empty()
        ? "worlds" : (cfg_.dataPath + "/worlds");
    std::string worldDir = worldsRoot + "/" + cfg_.name;
    ensureDir(worldsRoot);
    ensureDir(worldDir);

    // Create the world generator based on cfg_.generator name.
    // Mirrors Nukkit's Generator.getGenerator(name) + newInstance().
    if (cfg_.generator == "flat") {
        // Default flat preset: bedrock / 2x dirt / grass.
        std::vector<generator::FlatLayer> layers = {
            {7, 1},   // bedrock
            {3, 2},   // dirt
            {2, 1},   // grass
        };
        generator_ = std::make_shared<generator::Flat>(std::move(layers));
    } else if (cfg_.generator == "nether") {
        generator_ = std::make_shared<generator::Nether>(cfg_.seed);
    } else if (cfg_.generator == "void" || cfg_.generator == "empty") {
        generator_ = std::make_shared<generator::EmptyGenerator>();
    } else {
        // "normal" is the default generator.
        generator_ = std::make_shared<generator::Normal>(cfg_.seed);
    }

    // Get the spawn position from the generator.
    mcp_vector3_t gen_spawn = generator_->getSpawn();
    spawn_ = mcp_v3(gen_spawn.x, gen_spawn.y, gen_spawn.z);

    // Create a LevelProvider via the registry.
    auto& mgr = format::LevelProviderManager::getInstance();
    auto provider_unique = mgr.create(cfg_.format);
    if (!provider_unique) {
        utils::MainLogger::get().warn("Unknown level format '" + cfg_.format +
            "'; falling back to default '" + mgr.defaultFormatName() + "'");
        provider_unique = mgr.create(mgr.defaultFormatName());
    }
    std::shared_ptr<format::LevelProvider> provider = std::move(provider_unique);
    if (provider) {
        provider->init(worldDir);
        // Apply the generator's spawn to the provider.
        provider->setSpawn(math::Vector3{
            gen_spawn.x, gen_spawn.y, gen_spawn.z});
        provider->setCurrentTime(cfg_.time);
        // Read metadata that the provider may have loaded (seed, spawn).
        if (provider->getSeed() != 0) {
            cfg_.seed = provider->getSeed();
        } else {
            // World is brand-new: push the runtime-generated seed down
            // to the provider so it gets persisted in level.dat.
            provider->setSeed(cfg_.seed);
        }
        // If the provider loaded a spawn from level.dat (existing world),
        // use that instead of the generator default. This way the spawn
        // is preserved across restarts.
        if (provider->getSpawn().y > 0) {
            spawn_ = mcp_v3(provider->getSpawn().x + 0.5,
                              provider->getSpawn().y,
                              provider->getSpawn().z + 0.5);
        }
        utils::MainLogger::get().info("Level '" + cfg_.name + "' loaded from " +
            worldDir + " (format=" + provider->getName() +
            ", seed=" + std::to_string(cfg_.seed) +
            ", generator=" + cfg_.generator + ")");
    } else {
        utils::MainLogger::get().error("Failed to create level provider for format '" +
            cfg_.format + "' — chunks will be in-memory only!");
    }

    // Connect the provider to the ChunkManager so flushDirty() actually
    // writes to disk. Also pass the generator so ChunkManager can
    // generate new chunks using the correct terrain type (Normal/Flat/
    // Nether) instead of the old hardcoded flat chunk.
    chunks_.setProvider(provider);
    chunks_.setGenerator(generator_);
    provider_ = provider;
}

Level::~Level() {
    if (provider_) provider_->shutdown();
}

uint8_t Level::getBlockId(int32_t x, int32_t y, int32_t z) {
    int32_t cx = x >> 4, cz = z >> 4;
    mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
    if (!c) return 0;
    return mcp_chunk_get_block_id(c, x & 15, y, z & 15);
}

uint8_t Level::getBlockData(int32_t x, int32_t y, int32_t z) {
    int32_t cx = x >> 4, cz = z >> 4;
    mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
    if (!c) return 0;
    return mcp_chunk_get_block_data(c, x & 15, y, z & 15);
}

void Level::setBlock(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t data) {
    // CRITICAL FIX: previously this called chunks_.getChunk(cx, cz) which
    // triggers SYNCHRONOUS chunk generation on the main thread if the chunk
    // is not loaded. BlockFire::onScheduledUpdate and ItemFlintSteel call
    // setBlock() in tight loops (6 neighbours + 3x7x3 volume scan), and each
    // miss would generate a fresh chunk — stalling the tick loop for seconds
    // and producing the "server freezes when I use flint and steel" bug.
    //
    // We now use getChunkIfLoaded() and silently bail if the chunk isn't
    // resident. Callers that absolutely need the chunk to exist (e.g.
    // player block placement) should pre-load via ChunkManager::getChunk()
    // on the async path, not inline on the main thread.
    int32_t cx = x >> 4, cz = z >> 4;
    if (mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz)) {
        mcp_chunk_set_block_full(c, x & 15, y, z & 15, id, data);
        c->dirty = true;
    }
}

int Level::save() {
    // Write every dirty tile's NBT back into its chunk's tile_entities
    // array BEFORE flushing — flushDirty() is what actually serializes
    // each dirty chunk (tile_entities included) to the .mca region file,
    // so this has to happen first or the chunk gets saved with whatever
    // tile_entities blobs it already had (stale or empty).
    syncTilesToChunks();
    // Delegate to ChunkManager which calls provider_->saveChunk() for each
    // dirty chunk. The provider writes the actual .mca region files.
    int saved = chunks_.flushDirty();
    // Persist world metadata (level.dat) so seed/spawn/time survive
    // the next restart — mirroring Nukkit's Level.save() which calls
    // provider.saveLevelData() after flushing chunks.
    if (provider_) {
        provider_->setSpawn(math::Vector3{
            (double)spawn_.x, (double)spawn_.y, (double)spawn_.z});
        provider_->setCurrentTime(cfg_.time);
        provider_->saveLevelData();
    }
    return saved;
}

void Level::tick(uint64_t current_tick) {
    advanceTime(1);
    doWeatherTick(current_tick);
    // NOTE: the dropped-items sweep used to run here every tick. It was
    // moved to collectDroppedItems(), driven by the single global GC in
    // MemoryManager — running an independent reclaimer inside tick() freed
    // records out from under player pickup handlers (taken-flag race →
    // phantom pickups) and was one of the scattered GCs this codebase had.
    (void)current_tick;
}

size_t Level::collectDroppedItems(uint64_t current_tick) {
    constexpr uint64_t DESPAWN_TICKS = 6000;  // 5 min at 20 TPS — vanilla EntityItem lifetime

    // Phase 1: despawn expired items. Snapshot FIRST, then broadcast with
    // no registry lock held (lock-ordering rule used by every other
    // Level method that sends packets). This pass only ever runs on the
    // main thread between player updates (it is driven by the global GC in
    // Server::tick), so it can never interleave with a pickup handler that
    // holds a DroppedItem* snapshot. Additionally, if a pickup transaction
    // straddles a pass boundary, Player::onUpdate() re-checks the claim via
    // tryClaimDroppedItem (false once the eid is erased), so it rolls back
    // instead of duplicating.
    std::vector<int64_t> expired;
    {
        std::lock_guard<std::mutex> lk(dropped_items_mtx_);
        for (auto it = dropped_items_.begin(); it != dropped_items_.end(); ) {
            if (!it->second) { it = dropped_items_.erase(it); continue; }
            if (!it->second->taken &&
                current_tick - it->second->spawn_tick >= DESPAWN_TICKS) {
                expired.push_back(it->first);
                it = dropped_items_.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (!expired.empty()) {
        auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
        // One packet per eid — RemoveEntityPacket carries a single eid.
        for (size_t i = 0; i < expired.size(); ++i) {
            if (i > 0) rm = std::make_shared<network::protocol::RemoveEntityPacket>();
            rm->eid = expired[i];
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (p && p->spawned()) p->sendPacket(rm);
            }
        }
    }

    // Phase 2: drop fully-picked-up entries (the old tick() sweep job).
    // Safe: by the time a record is `taken`, the picking player has already
    // received its inventory + RemoveEntityPacket, and other players got
    // their TakeItemEntityPacket — nothing is mid-transaction on this
    // record anymore, so erasing it cannot desync anyone.
    size_t removed = expired.size();
    {
        std::lock_guard<std::mutex> lk(dropped_items_mtx_);
        for (auto it = dropped_items_.begin(); it != dropped_items_.end(); ) {
            if (it->second && it->second->taken) {
                it = dropped_items_.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
    }
    return removed;
}

size_t Level::tickTiles(uint64_t current_tick) {
    std::vector<std::shared_ptr<cursestone::tile::Tile>> snapshot;
    {
        std::lock_guard<std::mutex> lk(tiles_mtx_);
        snapshot.reserve(tiles_.size());
        for (auto& [_, t] : tiles_) snapshot.push_back(t);
    }
    size_t n = 0;
    for (auto& t : snapshot) {
        if (!t) continue;
        t->tick(current_tick);
        ++n;
    }
    return n;
}

void Level::registerLoader(uint64_t id, class ChunkLoader* loader) {
    loaders_[id] = loader;
}

void Level::unregisterLoader(uint64_t id) {
    loaders_.erase(id);
}

// ============================================================
// Dropped-item registry
// ============================================================
DroppedItem* Level::droppedItem(int64_t eid) {
    std::lock_guard<std::mutex> lk(dropped_items_mtx_);
    auto it = dropped_items_.find(eid);
    if (it == dropped_items_.end()) return nullptr;
    return it->second.get();
}

std::vector<DroppedItem*> Level::droppedItems() {
    std::lock_guard<std::mutex> lk(dropped_items_mtx_);
    std::vector<DroppedItem*> out;
    out.reserve(dropped_items_.size());
    for (auto& [_, di] : dropped_items_) {
        if (di && !di->taken) out.push_back(di.get());
    }
    return out;
}

void Level::addDroppedItem(const DroppedItem& di) {
    std::lock_guard<std::mutex> lk(dropped_items_mtx_);
    auto copy = std::make_unique<DroppedItem>(di);
    dropped_items_[di.eid] = std::move(copy);
}

void Level::removeDroppedItem(int64_t eid) {
    std::lock_guard<std::mutex> lk(dropped_items_mtx_);
    auto it = dropped_items_.find(eid);
    if (it != dropped_items_.end()) {
        it->second->taken = true;
    }
}

bool Level::tryClaimDroppedItem(int64_t eid) {
    std::lock_guard<std::mutex> lk(dropped_items_mtx_);
    auto it = dropped_items_.find(eid);
    if (it == dropped_items_.end()) return false;
    DroppedItem* di = it->second.get();
    if (!di || di->taken) return false;
    // Atomically claim: set taken=true while holding the lock.
    // This prevents a second player from also claiming the same item.
    di->taken = true;
    return true;
}

// ============================================================
// Tile-entity registry
// ============================================================
std::shared_ptr<cursestone::tile::Tile> Level::getTile(int32_t x, int32_t y, int32_t z) {
    ensureTilesLoaded(x >> 4, z >> 4);
    std::lock_guard<std::mutex> lk(tiles_mtx_);
    auto it = tiles_.find(packTilePos(x, y, z));
    if (it == tiles_.end()) return nullptr;
    return it->second;
}

void Level::setTile(int32_t x, int32_t y, int32_t z, std::shared_ptr<cursestone::tile::Tile> t) {
    std::lock_guard<std::mutex> lk(tiles_mtx_);
    tiles_[packTilePos(x, y, z)] = std::move(t);
}

void Level::removeTile(int32_t x, int32_t y, int32_t z) {
    std::lock_guard<std::mutex> lk(tiles_mtx_);
    tiles_.erase(packTilePos(x, y, z));
}

std::vector<std::shared_ptr<cursestone::tile::Tile>> Level::getTilesInChunk(int32_t cx, int32_t cz) {
    ensureTilesLoaded(cx, cz);
    std::vector<std::shared_ptr<cursestone::tile::Tile>> out;
    std::lock_guard<std::mutex> lk(tiles_mtx_);
    for (auto& kv : tiles_) {
        auto& t = kv.second;
        if (!t) continue;
        const auto& p = t->position();
        if (((int32_t)p.x >> 4) == cx && ((int32_t)p.z >> 4) == cz) {
            out.push_back(t);
        }
    }
    return out;
}

// ============================================================
// Tile-entity persistence (NBT <-> mcp_chunk_t::tile_entities)
// ============================================================
void Level::ensureTilesLoaded(int32_t cx, int32_t cz) {
    uint64_t ckey = packChunkPos(cx, cz);
    {
        std::lock_guard<std::mutex> lk(tiles_mtx_);
        if (tiles_hydrated_chunks_.count(ckey)) return;
        // Mark hydrated up front, even if the chunk isn't loaded yet or
        // has no tiles — this is a "have we looked" flag, not a "did we
        // find anything" flag. A chunk with zero chests shouldn't be
        // re-scanned on every single getTile() call either.
        tiles_hydrated_chunks_.insert(ckey);
    }
    mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
    if (!c || !c->tile_entities || c->tile_entity_count == 0) return;

    for (size_t i = 0; i < c->tile_entity_count; ++i) {
        const uint8_t* blob = c->tile_entities[i];
        size_t blen = c->tile_entity_lens ? c->tile_entity_lens[i] : 0;
        if (!blob || blen == 0) continue;
        // Blobs on mcp_chunk_t are stored little-endian, same convention
        // AnvilChunkCodec already uses for these same arrays.
        mcp_tag_t* tag = mcp_nbt_read_bytes(blob, blen, /*little_endian=*/true);
        if (!tag) continue;

        const char* id = mcp_compound_tag_get_string(tag, "id", "");
        int32_t tx = mcp_compound_tag_get_int(tag, "x", 0);
        int32_t ty = mcp_compound_tag_get_int(tag, "y", 0);
        int32_t tz = mcp_compound_tag_get_int(tag, "z", 0);

        std::shared_ptr<cursestone::tile::Tile> tile;
        cursestone::level::Position pos(tx, ty, tz);
        if (std::strcmp(id, "Chest") == 0) {
            tile = std::make_shared<cursestone::tile::TileChest>(pos);
        } else if (std::strcmp(id, "Sign") == 0) {
            tile = std::make_shared<cursestone::tile::TileSign>(pos);
        } else if (std::strcmp(id, "Furnace") == 0) {
            // El horno ahora persiste contenido real (3 slots + burn/
            // cook ticks — ver TileFurnace::writeNBT), así que se
            // reconstruye igual que el cofre.
            tile = std::make_shared<cursestone::tile::TileFurnace>(pos);
        }
        // Other tile ids (MobSpawner, ...) still live purely in the
        // in-memory registry via setTile().
        if (tile) {
            tile->readNBT(tag);
            std::lock_guard<std::mutex> lk(tiles_mtx_);
            tiles_[packTilePos(tx, ty, tz)] = std::move(tile);
        }
        mcp_tag_destroy(tag);
    }
}

void Level::syncTilesToChunks() {
    // Snapshot ALL live tiles (not just dirty ones) grouped by owning
    // chunk, released from the lock before touching chunks_ (which has
    // its own internal locking) to avoid a lock-order dependency between
    // tiles_mtx_ and ChunkManager's mutex.
    //
    // Why ALL tiles and not just dirty ones: a chunk's tile_entities
    // array is rebuilt from scratch below (mcp_chunk_clear_tile_entities
    // then re-add). If we only fed it the dirty tiles, a chunk with one
    // dirty chest and one untouched chest would lose the untouched
    // chest's blob entirely — clear_tile_entities doesn't know which
    // entries "should" survive, it just empties the array.
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<cursestone::tile::Tile>>> byChunk;
    bool anyDirty = false;
    {
        std::lock_guard<std::mutex> lk(tiles_mtx_);
        for (auto& [_, t] : tiles_) {
            if (!t) continue;
            if (t->isDirty()) anyDirty = true;
            int32_t cx = static_cast<int32_t>(t->position().x) >> 4;
            int32_t cz = static_cast<int32_t>(t->position().z) >> 4;
            byChunk[packChunkPos(cx, cz)].push_back(t);
        }
    }
    if (!anyDirty) return;  // nothing changed since the last save()

    for (auto& [ckey, tilesInChunk] : byChunk) {
        // Only rebuild a chunk's tile_entities array if something in it
        // actually changed — skips the write+serialize cost for every
        // chunk holding only untouched tiles.
        bool chunkHasDirty = false;
        for (auto& t : tilesInChunk) if (t->isDirty()) { chunkHasDirty = true; break; }
        if (!chunkHasDirty) continue;

        int32_t cx = static_cast<int32_t>(ckey >> 32);
        int32_t cz = static_cast<int32_t>(ckey & 0xFFFFFFFFULL);
        mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
        if (!c) continue;  // chunk got unloaded since the tile was touched — stays dirty, retried next save()

        mcp_chunk_clear_tile_entities(c);
        for (auto& t : tilesInChunk) {
            mcp_tag_t* tag = mcp_compound_tag_new();
            t->writeNBT(tag);
            size_t blen = 0;
            uint8_t* bbuf = mcp_nbt_write_bytes(tag, /*little_endian=*/true, &blen);
            if (bbuf && blen > 0) {
                mcp_chunk_add_tile_entity(c, bbuf, blen);
                t->clearDirty();  // only clear once actually written to the chunk
            }
            free(bbuf);
            mcp_tag_destroy(tag);
        }
        c->dirty = true;  // ensure flushDirty() picks this chunk up
    }
}

// ============================================================
// Chunk memory eviction
// ============================================================
void Level::evictTilesForChunk(int32_t cx, int32_t cz) {
    uint64_t ckey = packChunkPos(cx, cz);

    // Snapshot this chunk's tiles, then release tiles_mtx_ before doing
    // any NBT work or touching chunks_ — same lock-ordering rule
    // syncTilesToChunks() already follows above.
    std::vector<std::shared_ptr<cursestone::tile::Tile>> tilesInChunk;
    {
        std::lock_guard<std::mutex> lk(tiles_mtx_);
        for (auto it = tiles_.begin(); it != tiles_.end(); ) {
            if (!it->second) { it = tiles_.erase(it); continue; }
            int32_t tcx = static_cast<int32_t>(it->second->position().x) >> 4;
            int32_t tcz = static_cast<int32_t>(it->second->position().z) >> 4;
            if (tcx == cx && tcz == cz) {
                tilesInChunk.push_back(it->second);
                it = tiles_.erase(it);   // drop from the registry regardless of dirty state
            } else {
                ++it;
            }
        }
        tiles_hydrated_chunks_.erase(ckey);  // force re-hydration on next load
    }
    if (tilesInChunk.empty()) return;

    bool anyDirty = false;
    for (auto& t : tilesInChunk) if (t->isDirty()) { anyDirty = true; break; }
    if (!anyDirty) return;  // nothing to flush — they were dropped above either way

    mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
    if (!c) return;  // chunk is gone already; the (unsaved) dirty tile data is lost with it

    mcp_chunk_clear_tile_entities(c);
    for (auto& t : tilesInChunk) {
        mcp_tag_t* tag = mcp_compound_tag_new();
        t->writeNBT(tag);
        size_t blen = 0;
        uint8_t* bbuf = mcp_nbt_write_bytes(tag, /*little_endian=*/true, &blen);
        if (bbuf && blen > 0) {
            mcp_chunk_add_tile_entity(c, bbuf, blen);
        }
        free(bbuf);
        mcp_tag_destroy(tag);
    }
    c->dirty = true;  // so ChunkManager::unloadChunk()'s save-if-dirty picks it up
}

int Level::unloadDistantChunks(const std::vector<ChunkViewer>& viewers, int32_t margin) {
    auto coords = chunks_.loadedChunkCoords();
    int unloaded = 0;
    for (auto& [cx, cz] : coords) {
        bool needed = false;
        for (auto& v : viewers) {
            int32_t dx = cx - v.cx, dz = cz - v.cz;
            int32_t dist = std::max(std::abs(dx), std::abs(dz));
            if (dist <= v.view_radius + margin) { needed = true; break; }
        }
        if (needed) continue;

        evictTilesForChunk(cx, cz);  // flush + drop tiles BEFORE the chunk disappears
        if (chunks_.unloadChunk(cx, cz, /*save=*/true)) ++unloaded;
    }
    return unloaded;
}

// ============================================================
// Weather system — ported from Dr1xyDev/Infinity Weather.php
// ============================================================
// Wire format (protocol 84 / MCPE 0.15.10, verified against
// Dr1xyDev/Infinity LevelEventPacket.php + Weather.php):
//   - evid 3001 EVENT_START_RAIN, 3002 EVENT_START_THUNDER,
//     3003 EVENT_STOP_RAIN,     3004 EVENT_STOP_THUNDER.
//   - The `data` field MUST carry a non-zero strength or the 0.15.10
//     client renders nothing (clear sky): rain rolls 90000..110000,
//     thunder 30000..40000.
//   - Infinity sends the pair as reset-then-set: STOP_RAIN +
//     STOP_THUNDER always first, then the START events for the new
//     state. Re-sending the full pair is idempotent on the client and
//     guarantees a clean transition between any two states.
void Level::sendWeatherTo(Player* p) {
    if (!p) return;
    auto stop_rain = std::make_shared<network::protocol::LevelEventPacket>();
    stop_rain->evid = network::protocol::LevelEventPacket::EVENT_STOP_RAIN;
    stop_rain->data = (int32_t)rain_strength_;
    p->sendPacket(stop_rain);

    auto stop_thunder = std::make_shared<network::protocol::LevelEventPacket>();
    stop_thunder->evid = network::protocol::LevelEventPacket::EVENT_STOP_THUNDER;
    stop_thunder->data = (int32_t)thunder_strength_;
    p->sendPacket(stop_thunder);

    if (isRaining()) {
        auto start = std::make_shared<network::protocol::LevelEventPacket>();
        start->evid = network::protocol::LevelEventPacket::EVENT_START_RAIN;
        start->data = (int32_t)rain_strength_;
        p->sendPacket(start);
    }
    if (isThundering()) {
        auto start = std::make_shared<network::protocol::LevelEventPacket>();
        start->evid = network::protocol::LevelEventPacket::EVENT_START_THUNDER;
        start->data = (int32_t)thunder_strength_;
        p->sendPacket(start);
    }
}

void Level::setWeather(int state, int32_t durationTicks) {
    if (state < WEATHER_CLEAR || state > WEATHER_THUNDER) state = WEATHER_CLEAR;
    bool changed = (weather_state_ != state);
    weather_state_ = state;
    weather_ticks_left_ = durationTicks > 0 ? durationTicks : -1;
    // Re-roll the strengths on every change, like Infinity's setWeather
    // (mt_rand(90000,110000) / mt_rand(30000,40000)).
    rain_strength_    = 90000 + (uint32_t)(rand() % 20001);
    thunder_strength_ = 30000 + (uint32_t)(rand() % 10001);
    for (auto& p : Server::getInstance().playersSnapshot()) {
        if (p && p->spawned()) sendWeatherTo(p.get());
    }
    if (changed) {
        static const char* kNames[4] = { "clear", "rain", "rain+thunder", "thunder" };
        utils::MainLogger::get().info(std::string("[Weather] Level '") + cfg_.name +
            "' -> " + kNames[state] +
            (weather_ticks_left_ > 0
                ? " (" + std::to_string(weather_ticks_left_ / 20) + "s)"
                : ""));
    }
}

// ---------------------------------------------------------------
// Lightning — ported from Dr1xyDev/Infinity Lightning.php
// ---------------------------------------------------------------
namespace {

// Removes the bolt entity ~1 second after the strike (Infinity:
// onUpdate -> age > 20 ticks -> kill/close). Runs on the main thread
// via the scheduler, same constraints as every other sync task.
class LightningRemovalTask : public scheduler::Task {
public:
    explicit LightningRemovalTask(int64_t eid) : eid_(eid) {}
    void onRun() override {
        auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
        rm->eid = eid_;
        for (auto& p : Server::getInstance().playersSnapshot()) {
            if (p && p->spawned()) p->sendPacket(rm);
        }
    }
private:
    int64_t eid_;
};

} // namespace

void Level::spawnLightning(double x, double y, double z, bool withFire) {
    int64_t eid = Server::getInstance().allocateEid();

    // Bolt + shockwave to every online player (Infinity spawnTo:
    // AddEntityPacket type 93 + ExplodePacket radius 10, records=[]).
    auto add = std::make_shared<network::protocol::AddEntityPacket>();
    add->eid = eid;
    add->type = network::protocol::AddEntityPacket::TYPE_LIGHTNING;
    add->x = (float)x; add->y = (float)y; add->z = (float)z;
    auto exp = std::make_shared<network::protocol::ExplodePacket>();
    exp->x = (float)x; exp->y = (float)y; exp->z = (float)z;
    exp->radius = 10.0f;
    for (auto& p : Server::getInstance().playersSnapshot()) {
        if (p && p->spawned()) {
            p->sendPacket(add);
            p->sendPacket(exp);
        }
    }

    // Despawn (Infinity: age > 20 ticks).
    scheduler::ServerScheduler::get().scheduleSync(
        std::make_shared<LightningRemovalTask>(eid), 20);

    // Damage + ignite players inside Infinity's grow(4, 3, 4) box:
    // mt_rand(8, 20) damage, setOnFire(mt_rand(3, 8)) — survival only.
    if (Server::getInstance().difficulty() > 0) {
        for (auto& p : Server::getInstance().playersSnapshot()) {
            if (!p || !p->spawned()) continue;
            const auto& pos = p->getNextPosition();
            if (std::fabs(pos.x - x) > 4.0 || std::fabs(pos.y - y) > 3.0 ||
                std::fabs(pos.z - z) > 4.0) continue;
            p->attack(8.0f + (float)(std::rand() % 13));
            p->setOnFire(3 + std::rand() % 6);
        }
    }

    // Ground fire (Infinity lightningFire): fire at the strike position
    // when the block there is replaceable. With rain active the sweep
    // will put it out shortly after — same as vanilla.
    if (withFire) {
        int32_t bx = (int32_t)std::floor(x);
        int32_t bz = (int32_t)std::floor(z);
        int32_t by = (int32_t)std::floor(y);
        if (by >= 0 && by <= 255 && isChunkLoaded(bx, bz)) {
            uint8_t at = getBlockId(bx, by, bz);
            uint8_t below = by > 0 ? getBlockId(bx, by - 1, bz) : block::AIR;
            bool below_liquid = (below == block::WATER || below == block::FLOWING_WATER ||
                                 below == block::LAVA  || below == block::FLOWING_LAVA);
            if ((at == block::AIR || at == block::TALL_GRASS) && !below_liquid) {
                setBlock(bx, by, bz, block::FIRE, 0);
                auto up = std::make_shared<network::protocol::UpdateBlockPacket>();
                up->x = bx; up->z = bz;
                up->y = (uint8_t)by;
                up->blockId = block::FIRE;
                up->flags = network::protocol::UpdateBlockPacket::FLAG_ALL;
                for (auto& p : Server::getInstance().playersSnapshot()) {
                    if (p && p->spawned()) p->sendPacket(up);
                }
            }
        }
    }
}

void Level::strikeRandomPlayerLightning() {
    // Infinity Weather::calcWeather: pick a random player, strike at
    // (p.x + mt_rand(-64, 64), highestBlock, p.z + mt_rand(-64, 64)).
    auto players = Server::getInstance().playersSnapshot();
    Player* target = nullptr;
    for (auto& p : players) {
        if (p && p->spawned()) { target = p.get(); break; }
    }
    if (!target) return;

    const auto& tpos = target->getNextPosition();
    int32_t bx = (int32_t)std::floor(tpos.x) + (std::rand() % 129 - 64);
    int32_t bz = (int32_t)std::floor(tpos.z) + (std::rand() % 129 - 64);
    if (!isChunkLoaded(bx, bz)) return;   // strike fell outside loaded world
    mcp_chunk_t* c = chunks_.getChunkIfLoaded(bx >> 4, bz >> 4);
    if (!c || !c->height_map) return;
    // height_map = topmost light-blocking block + 1 => first AIR y.
    int32_t y = c->height_map[((bz & 15) << 4) | (bx & 15)];
    if (y <= 0 || y > 255) return;
    spawnLightning((double)bx + 0.5, (double)y, (double)bz + 0.5, /*withFire=*/true);
}

// ---------------------------------------------------------------
// doWeatherTick — called from Level::tick() every server tick.
//
//  1. Counts down rain/thunder durations (when bounded).
//  2. While raining, extinguishes exposed fire: walks a rotating
//     window of CHUNKS_PER_TICK loaded chunks per tick and replaces
//     every FIRE block whose column has sky above it with AIR.
//     Chunk scans read section->blocks directly (4096 bytes each,
//     no per-block accessor call); per-column sky checks use the
//     height map, so "exposed" means "sky directly above", matching
//     how the rain visibly falls. Vanilla PMMP also has a chance
//     component; we keep it deterministic here and cap the work per
//     tick (MAX_EXTINGUISH_PER_TICK) so a large burning area can
//     never stall the tick loop (the same philosophy that killed
//     fire propagation in v6).
//  3. Sends UpdateBlockPacket to every online player for each block
//     changed, so clients see the fire die instead of finding out on
//     the next chunk resync.
// ---------------------------------------------------------------
void Level::doWeatherTick(uint64_t current_tick) {
    // 1. Duration countdown.
    if (weather_ticks_left_ > 0) {
        if (--weather_ticks_left_ <= 0) setWeather(WEATHER_CLEAR);
    }

    // 1.5. Lightning strikes (Infinity Weather::calcWeather strikes
    // when weatherNow >= RAINY_THUNDER, i.e. states 2 AND 3 — this is
    // also why the check sits above the isRaining() early-return:
    // state 3 thunders without rain). Every LIGHTNING_STRIKE_INTERVAL
    // ticks, strike within ±64 blocks of a random player. With no
    // players online the timer just keeps sliding forward.
    if (isThundering() && current_tick >= next_lightning_tick_) {
        next_lightning_tick_ = current_tick + LIGHTNING_STRIKE_INTERVAL;
        strikeRandomPlayerLightning();
    }

    if (!isRaining()) return;

    // 2. Fire sweep (only while raining).
    auto coords = chunks_.loadedChunkCoords();
    if (coords.empty()) return;
    int extinguished = 0;
    for (int i = 0; i < WEATHER_CHUNKS_PER_TICK && extinguished < MAX_EXTINGUISH_PER_TICK; ++i) {
        const auto& [cx, cz] = coords[(rain_scan_offset_ + (uint64_t)i) % coords.size()];
        mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
        if (!c) continue;

        for (uint8_t sy = 0; sy < 16; ++sy) {
            mcp_chunk_section_t* sec = c->sections[sy];
            if (!sec || !sec->blocks) continue;
            // Cheap pre-filter: is there ANY fire in this 4096-block
            // section? Avoids 4096 iterations on every section of
            // every chunk when there is no fire anywhere.
            if (!std::memchr(sec->blocks, (int)block::FIRE, 4096)) continue;

            // BUG FIX ("extinguished 6 fire block(s)" spam, fire never
            // dies): the scan previously wrapped the j-loop in an extra
            // `ly` 0..15 loop that re-walked the whole 4096-block array
            // 16 times, while deriving the world Y from `ly` instead of
            // from the hit index. The blocks array is XZY-packed
            // (x | z<<4 | y<<8), so j>>8 IS the section-relative y — the
            // old code set AIR 16 blocks below the real fire and counted
            // the same fire once per mismatched ly, so the memory never
            // changed and the log repeated forever. y now comes from j.
            for (int j = 0; j < 4096 && extinguished < MAX_EXTINGUISH_PER_TICK; ++j) {
                if (sec->blocks[j] != (uint8_t)block::FIRE) continue;
                int lx = j & 15;
                int lz = (j >> 4) & 15;
                int32_t y = ((int32_t)sy << 4) | (j >> 8);
                // Exposed = sky visible above this column. The height
                // map stores topmost light-blocking block + 1 per
                // column, and FIRE itself never inflates it
                // (mcp_block_light_opacity(FIRE) == 0), so a fire
                // whose top face touches open sky has y >= h here.
                int32_t h = c->height_map ? c->height_map[(lz << 4) | lx] : 256;
                if (y < h) continue;
                int32_t wx = (cx << 4) | lx;
                int32_t wz = (cz << 4) | lz;
                // Route through Level::setBlock so the chunk dirty
                // flag is set (and stays on the same write path as
                // the rest of the server).
                setBlock(wx, y, wz, block::AIR, 0);
                auto up = std::make_shared<network::protocol::UpdateBlockPacket>();
                up->x = wx; up->z = wz;
                up->y = (uint8_t)y;
                up->blockId = block::AIR;
                up->blockData = 0;
                up->flags = network::protocol::UpdateBlockPacket::FLAG_ALL;
                for (auto& p : Server::getInstance().playersSnapshot()) {
                    if (p && p->spawned()) p->sendPacket(up);
                }
                ++extinguished;
            }
        }
    }
    rain_scan_offset_ += WEATHER_CHUNKS_PER_TICK;

    // 3. One rate-limited log line so operators can SEE the feature work.
    if (extinguished > 0 &&
        current_tick - last_fire_extinguish_log_ >= FIRE_EXTINGUISH_LOG_INTERVAL) {
        last_fire_extinguish_log_ = current_tick;
        utils::MainLogger::get().info(
            "[Weather] Rain extinguished " + std::to_string(extinguished) +
            " fire block(s) in level '" + cfg_.name + "'");
    }
}

} // namespace cursestone::level
