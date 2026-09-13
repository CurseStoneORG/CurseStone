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
#include "tile/TileSign.h"
#include "nbt/NBT.h"
#include "nbt/tag/CompoundTag.h"
#include "Player.h"
#include "Server.h"
#include "block/BlockIds.h"
#include "block/BlockFire.h"
#include "network/protocol/LevelEventPacket.h"
#include "network/protocol/UpdateBlockPacket.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

namespace atomikpe::level {

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
    // Sweep dropped-items registry: erase any items marked as taken.
    // This avoids the map growing unbounded as players pick up items.
    {
        std::lock_guard<std::mutex> lk(dropped_items_mtx_);
        for (auto it = dropped_items_.begin(); it != dropped_items_.end(); ) {
            if (it->second && it->second->taken) {
                it = dropped_items_.erase(it);
            } else {
                ++it;
            }
        }
    }
    (void)current_tick;
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
std::shared_ptr<atomikpe::tile::Tile> Level::getTile(int32_t x, int32_t y, int32_t z) {
    ensureTilesLoaded(x >> 4, z >> 4);
    std::lock_guard<std::mutex> lk(tiles_mtx_);
    auto it = tiles_.find(packTilePos(x, y, z));
    if (it == tiles_.end()) return nullptr;
    return it->second;
}

void Level::setTile(int32_t x, int32_t y, int32_t z, std::shared_ptr<atomikpe::tile::Tile> t) {
    std::lock_guard<std::mutex> lk(tiles_mtx_);
    tiles_[packTilePos(x, y, z)] = std::move(t);
}

void Level::removeTile(int32_t x, int32_t y, int32_t z) {
    std::lock_guard<std::mutex> lk(tiles_mtx_);
    tiles_.erase(packTilePos(x, y, z));
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

        std::shared_ptr<atomikpe::tile::Tile> tile;
        atomikpe::level::Position pos(tx, ty, tz);
        if (std::strcmp(id, "Chest") == 0) {
            tile = std::make_shared<atomikpe::tile::TileChest>(pos);
        } else if (std::strcmp(id, "Sign") == 0) {
            tile = std::make_shared<atomikpe::tile::TileSign>(pos);
        }
        // Other tile ids (Furnace, MobSpawner, ...) don't persist real
        // content yet — see Tile::writeNBT's base no-op — so there's
        // nothing meaningful to reconstruct for them here. They keep
        // living purely in the in-memory registry via setTile(), same
        // as before this change.
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
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<atomikpe::tile::Tile>>> byChunk;
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
    std::vector<std::shared_ptr<atomikpe::tile::Tile>> tilesInChunk;
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
// Weather system — estado + lluvia que apaga fuego
// ============================================================
// Wire format note (protocol 84): the client has NO dedicated weather
// packet. Rain is controlled via LevelEventPacket evidence 3001 (start)
// / 3002 (stop), thunder adds 3003 / 3004 on top. All coordinates and
// the data field are sent as zero — mirrors what PMMP broadcastLevelEvent
// callers pass for these events.
static void broadcastWeatherEvent(bool raining, bool thundering) {
    auto& srv = Server::getInstance();
    auto start = raining
        ? network::protocol::LevelEventPacket::EVENT_START_RAIN
        : network::protocol::LevelEventPacket::EVENT_STOP_RAIN;
    auto pk = std::make_shared<network::protocol::LevelEventPacket>();
    pk->evid = start;
    for (auto& p : srv.playersSnapshot()) {
        if (p && p->spawned()) p->sendPacket(pk);
    }
    if (thundering) {
        auto tp = std::make_shared<network::protocol::LevelEventPacket>();
        tp->evid = network::protocol::LevelEventPacket::EVENT_START_THUNDER;
        for (auto& p : srv.playersSnapshot()) {
            if (p && p->spawned()) p->sendPacket(tp);
        }
    }
}

void Level::setRaining(bool raining, int32_t durationTicks) {
    if (raining_ == raining) return;          // no-op — evita re-broadcasts
    raining_ = raining;
    rain_ticks_left_ = durationTicks > 0 ? durationTicks : -1;
    if (!raining) thundering_ = false;        // sin lluvia no puede haber trueno
    broadcastWeatherEvent(raining_, thundering_);
    utils::MainLogger::get().info(raining
        ? "[Weather] Rain started in level '" + cfg_.name + "'"
        : "[Weather] Rain stopped in level '" + cfg_.name + "'");
}

void Level::setThundering(bool thundering, int32_t durationTicks) {
    if (thundering_ == thundering) return;
    if (thundering && !raining_) {
        // Trueno sin lluvia no existe — enciende la lluvia primero.
        setRaining(true, durationTicks);
    }
    thundering_ = thundering;
    thunder_ticks_left_ = durationTicks > 0 ? durationTicks : -1;
    auto tp = std::make_shared<network::protocol::LevelEventPacket>();
    tp->evid = thundering
        ? network::protocol::LevelEventPacket::EVENT_START_THUNDER
        : network::protocol::LevelEventPacket::EVENT_STOP_THUNDER;
    for (auto& p : Server::getInstance().playersSnapshot()) {
        if (p && p->spawned()) p->sendPacket(tp);
    }
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
    // 1. Durations.
    if (raining_ && rain_ticks_left_ > 0) {
        if (--rain_ticks_left_ <= 0) setRaining(false);
    }
    if (thundering_ && thunder_ticks_left_ > 0) {
        if (--thunder_ticks_left_ <= 0) setThundering(false);
    }
    if (!raining_) return;

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

            for (int ly = 0; ly < 16 && extinguished < MAX_EXTINGUISH_PER_TICK; ++ly) {
                int32_t y = (int32_t)sy * 16 + ly;
                for (int j = 0; j < 4096 && extinguished < MAX_EXTINGUISH_PER_TICK; ++j) {
                    if (sec->blocks[j] != (uint8_t)block::FIRE) continue;
                    int lx = j & 15;
                    int lz = (j >> 4) & 15;
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

} // namespace atomikpe::level
