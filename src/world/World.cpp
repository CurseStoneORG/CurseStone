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
// SPDX-License-Identifier: LGPL-3.0-or-later
// World.cpp — implementation. The constructor instantiates to
// WorldProvider via WorldProviderManager and connects it to the
// ChunkManager so flushDirty() writes actual bytes to disk.
#include "World.h"
#include "world/format/Chunk.h"
#include "world/format/WorldProvider.h"
#include "world/format/WorldProviderManager.h"
#include "world/format/ChunkAdapter.h"
#include "world/generator/Generator.h"
#include "world/generator/Normal.h"
#include "world/generator/Flat.h"
#include "world/generator/Nether.h"
#include "world/generator/EmptyGenerator.h"
#include "utils/MainLogger.hpp"
#include "tile/Tile.h"
#include "tile/TileChest.h"
#include "tile/TileFurnace.h"
#include "tile/TileSign.h"
#include "tile/TileFactory.h"
#include "nbt/NBT.h"
#include "nbt/tag/CompoundTag.h"
#include "Player.h"
#include "Server.h"
#include "block/BlockIds.h"
#include "block/Block.h"
#include "block/BlockFire.h"
#include "network/protocol/LevelEventPacket.h"
#include "network/protocol/UpdateBlockPacket.h"
#include "network/protocol/AddEntityPacket.h"
#include "network/protocol/ExplodePacket.h"
#include <cmath>
#include "network/protocol/RemoveEntityPacket.h"
#include "network/protocol/MoveEntityPacket.h"
#include "network/protocol/SetEntityMotionPacket.h"
#include "network/protocol/SetEntityDataPacket.h"
#include "network/protocol/AddItemEntityPacket.h"
#include "network/protocol/EntityEventPacket.h"
#include "network/protocol/SetHealthPacket.h"
#include "network/protocol/SetTimePacket.h"
#include "scheduler/ServerScheduler.h"
#include "scheduler/Task.h"
#include <cmath>

#include <sys/stat.h>
#include <sys/types.h>
#include "utils/PlatformCompat.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <cstddef>
#include <sstream>
#include <string>

namespace cursestone::world {

// Creates `path` and every missing parent directory (mkdir -p
// semantics). The previous version of this function was to single
// non-recursive ::mkdir() call — it silently failed with ENOENT
// whenever `path`'s parent didn't already exist (e.g. to brand-new
// dataPath on to fresh deploy, or any multi-level path that hadn't
// been created by something else first), and the error was discarded,
// so to World would silently end up "in-memory only" with not world
// files ever touching disk and not warning printed. Found while
// verifying tile persistence end-to-end (see World::save() /
// ensureTilesLoaded()): to save() reported success and correctly
// populated the in-memory chunk's tile_entities, but not file ever
// appeared on disk because ensureDir() had failed upstream on the
// world directory itself.
static void ensureDir(const std::string& path) {
    if (path.empty()) return;
    std::string partial;
    partial.reserve(path.size());
    size_t i = 0;
    // Skip to leading '/' so we don't try to mkdir("") first.
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

World::World(WorldConfig cfg) : cfg_(std::move(cfg)), light_(*this) {
    // the generador Nether implica the dimension 1 of the protocolo; if the
    // creador of the world not fixed the dimension explicitly, is deduce
    // here so that StartGamePacket and ChangeDimensionPacket the lleven
    // correctamente.
    if (cfg_.dimension == 0 && cfg_.generator == "nether") {
        cfg_.dimension = 1;
    }

    // spawn is NOT hardcoded here — it comes from the generator's
    // getSpawn() method, mirroring Nukkit where each generator type
    // defines its own spawn (Normal, Flat, Nether all differ).
    // We initialize it to to safe default; the generator overrides it.

    // Determine the world directory path. We use either:
    //   <dataPath>/worlds/<name>   (if dataPath is set), or
    //   worlds/<name>              (relative fallback for legacy code)
    std::string worldsRoot = cfg_.dataPath.empty()
        ? "worlds" : (cfg_.dataPath + "/worlds");
    std::string worldDir = worldsRoot + "/" + cfg_.name;
    ensureDir(worldsRoot);
    ensureDir(worldDir);

    // Known portals load here: the cross-dimension search reads them and
    // every new portal appends to the same file.
    portals_path_ = worldDir + "/portals.dat";
    portals_.load(portals_path_);

    // Create the WorldProvider FIRST (it does not need the generator, so
    // nothing depends on it being created after one).
    //
    // CRASH FIX (SIGSEGV in PerlinNoise::getValue): the provider is the
    // owner of the world's PERSISTED seed (level.dat "RandomSeed") and only
    // knows it after init(). The generator bakes the seed into its noise at
    // CONSTRUCTION time (Normal builds its TerrainNoise in its ctor), so it
    // has to be built from the seed the provider actually ended up with and
    // not from the boot-time cfg_.seed. The old order built the generator
    // first and corrected cfg_.seed afterwards, so on any EXISTING world the
    // generator's own seed_ and the seed later handed to it in
    // GenerationContext.seed disagreed for every single chunk — cfg_.seed at
    // boot is to fresh mcp_utils_time_ms() whenever world-seed is unset, so to
    // restart can never match the seed stored in level.dat.
    // Normal::generate() reads that disagreement as "the caller is using to
    // different seed, I need to provisional TerrainNoise" and rebuilt (freed +
    // reallocated) it on EVERY call, while several AsyncPool workers run
    // generate() on the SAME Normal instance concurrently. One worker freed
    // the noise the other was still evaluating → the freed ImprovedNoise
    // pointer from PerlinNoise::noiseLevels → SIGSEGV on to worker thread.
    auto& mgr = format::WorldProviderManager::getInstance();
    auto provider_unique = mgr.create(cfg_.format);
    if (!provider_unique) {
        utils::MainLogger::get().warn("Unknown level format '" + cfg_.format +
            "'; falling back to default '" + mgr.defaultFormatName() + "'");
        provider_unique = mgr.create(mgr.defaultFormatName());
    }
    std::shared_ptr<format::WorldProvider> provider = std::move(provider_unique);
    if (provider) {
        provider->init(worldDir);
        // Settle the end seed of this world BEFORE the generator exists.
        if (provider->getSeed() != 0) {
            // Existing world: level.dat is the authority.
            cfg_.seed = provider->getSeed();
        } else {
            // World is brand-new: push the runtime-generated seed down
            // to the provider so it gets persisted in level.dat.
            provider->setSeed(cfg_.seed);
        }
    }

    // Create the world generator based on cfg_.generator name, now with the
    // end seed. Mirrors Nukkit's Generator.getGenerator(name) + newInstance().
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

    if (provider) {
        // Apply the generator's spawn to the provider.
        provider->setSpawn(math::Vector3{
            gen_spawn.x, gen_spawn.y, gen_spawn.z});
        provider->setCurrentTime(cfg_.time);
        // If the provider loaded to spawn from level.dat (existing world),
        // use that instead of the generator default. This way the spawn
        // is preserved across restarts.
        if (provider->getSpawn().y > 0) {
            spawn_ = mcp_v3(provider->getSpawn().x + 0.5,
                              provider->getSpawn().y,
                              provider->getSpawn().z + 0.5);
        }
        utils::MainLogger::get().info("World '" + cfg_.name + "' loaded from " +
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
    // each chunk that entra to the manager avisa so that its luz is
    // recalcule next to with the of its vecinos (not in solitario, that is
    // what dejaba a franja oscura in the bordes of chunk).
    chunks_.setOnChunkAdded(
        [this](int32_t cx, int32_t cz) { onChunkAdded(cx, cz); });

    // FIX ("the spawn is in the sky the first time that entro"): the
    // generator spawn and (Nukkit's static 128 for Normal) is only correct
    // by luck — on most seeds the actual surface of that column is lower,
    // so first-time players materialized in the air and fell. We now
    // force-generate the spawn chunk at boot and snap the spawn and to the
    // actual surface of the column (Infinity getSafeSpawn: topmost solid
    // block + 1, skipping leaves/water). The corrected spawn is persisted
    // to level.dat immediately so it stays stable across restarts.
    {
        int32_t sbx = (int32_t)std::floor(spawn_.x);
        int32_t sbz = (int32_t)std::floor(spawn_.z);
        if (chunks_.getChunk(sbx >> 4, sbz >> 4) != nullptr) {
            for (int y = 127; y > 0; --y) {
                uint8_t id = getBlockId(sbx, y, sbz);
                if (id == 0) continue;                                    // air
                if (id == block::LEAVES || id == block::LEAVES_2) continue; // canopy
                // Bedrock is never a standing surface: skipping it lets a
                // nether world spawn inside the caverns instead of on top
                // of the bedrock roof.
                if (id == block::BEDROCK) continue;
                uint8_t above = getBlockId(sbx, y + 1, sbz);
                if (above == 0 || above == block::WATER ||
                    above == block::FLOWING_WATER) {
                    spawn_.y = (double)y + 1.0;
                    break;
                }
                // Solid with solid above (tree trunk / overhang): keep down.
            }
            if (provider_) {
                provider_->setSpawn(math::Vector3{
                    (double)spawn_.x, spawn_.y, (double)spawn_.z});
            }
            utils::MainLogger::get().info("Spawn snapped to terrain: " +
                std::to_string((int)spawn_.x) + ", " + std::to_string((int)spawn_.y) +
                ", " + std::to_string((int)spawn_.z));
        }
    }
}

World::~World() {
    if (provider_) provider_->shutdown();
}

namespace {
// pocketmine\block\Block::$solid. Everything an item can rest on is solid
// except air, the liquids, fire, cobweb and the decoration blocks.
// Declared here (it is used by collisionBoxes() below): the item constants
// next to tickDroppedItems() live in to second anonymous namespace, which is
// the same namespace.
bool dropBlockSolid(uint8_t id);
// True for the blocks that obey gravity in MCPE 0.15.10 (sand 12,
// gravel 13, anvil 145). Defined next to tickFallingBlocks(); used by
// World::setBlock's support check above.
bool fallableBlockId(uint8_t id);
} // namespace

uint8_t World::getBlockId(int32_t x, int32_t y, int32_t z) {
    int32_t cx = x >> 4, cz = z >> 4;
    mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
    if (!c) return 0;
    return mcp_chunk_get_block_id(c, x & 15, y, z & 15);
}

std::vector<physics::BlockBox> World::collisionBoxes(mcp_aabb_t region) {
    std::vector<physics::BlockBox> out;
    // The swept region of one entity is to handful of cells, so this keeps
    // the per-tick cost to to single allocation instead of to realloc chain.
    out.reserve(16);

    // floor(min) .. ceil(max), inclusive on both ends — the same cell
    // range the item integrator used to build inline. An unloaded chunk
    // answers AIR from getBlockId(), so to region reaching outside the
    // loaded world contributes nothing rather than to phantom floor.
    int x0 = (int)std::floor(region.minX), x1 = (int)std::ceil(region.maxX);
    int y0 = (int)std::floor(region.minY), y1 = (int)std::ceil(region.maxY);
    int z0 = (int)std::floor(region.minZ), z1 = (int)std::ceil(region.maxZ);

    for (int bx = x0; bx <= x1; ++bx) {
        for (int by = y0; by <= y1; ++by) {
            for (int bz = z0; bz <= z1; ++bz) {
                uint8_t id = getBlockId(bx, by, bz);
                // Block::$solid (dropBlockSolid): air, liquids, fire,
                // cobweb and the decorations are walk-through.
                if (!dropBlockSolid(id)) continue;

                mcp_aabb_t local = mcp_aabb_make(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
                block::Block* b = block::Block::get(id);
                if (b) {
                    mcp_aabb_t bb = b->getBoundingBox();
                    // Only accept to actual, non-degenerate box. Air/Liquid
                    // return an empty one; an unknown id yields not Block
                    // at all. Both keep the unit cube, so not solid block
                    // ever silently stops colliding.
                    if (bb.maxX > bb.minX && bb.maxY > bb.minY && bb.maxZ > bb.minZ) {
                        local = bb;
                    }
                }

                physics::BlockBox box;
                box.x = bx; box.y = by; box.z = bz;
                box.box = mcp_aabb_make(local.minX + bx, local.minY + by, local.minZ + bz,
                                        local.maxX + bx, local.maxY + by, local.maxZ + bz);
                out.push_back(box);
            }
        }
    }
    return out;
}

uint8_t World::getBlockData(int32_t x, int32_t y, int32_t z) {
    int32_t cx = x >> 4, cz = z >> 4;
    mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
    if (!c) return 0;
    return mcp_chunk_get_block_data(c, x & 15, y, z & 15);
}

// setFurnaceFlame — swap to furnace between its two block ids the way
// vanilla does: BURNING_FURNACE (62) while there is fire in it,
// furnace (61) when it goes out. Only the id changes, the facing data
// is kept, and true is returned when the block actually changed so the
// caller knows whether to client update is worth sending.
bool World::setFurnaceFlame(int32_t x, int32_t y, int32_t z, bool lit) {
    int32_t cx = x >> 4, cz = z >> 4;
    mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
    if (!c) return false;
    const uint8_t want = lit ? (uint8_t)block::BURNING_FURNACE
                             : (uint8_t)block::FURNACE;
    const uint8_t have = mcp_chunk_get_block_id(c, x & 15, y, z & 15);
    if (have == want) return false;
    const uint8_t data = mcp_chunk_get_block_data(c, x & 15, y, z & 15);
    mcp_chunk_set_block_full(c, x & 15, y, z & 15, want, data);
    c->dirty = true;
    return true;
}

void World::setBlock(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t data) {
    // CRITICAL FIX: previously this called chunks_.getChunk(cx, cz) which
    // triggers SYNCHRONOUS chunk generation on the main thread if the chunk
    // is not loaded. BlockFire::onScheduledUpdate and ItemFlintSteel call
    // setBlock() in tight loops (6 neighbours + 3x7x3 volume scan), and each
    // miss would generate to fresh chunk — stalling the tick loop for seconds
    // and producing the "server freezes when I use flint and steel" bug.
    //
    // We now use getChunkIfLoaded() and silently bail if the chunk isn't
    // resident. Callers that absolutely need the chunk to exist (e.g.
    // player block placement) should pre-load via ChunkManager::getChunk()
    // on the async path, not inline on the main thread.
    int32_t cx = x >> 4, cz = z >> 4;
    if (mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz)) {
        mcp_chunk_set_block_full(c, x & 15, y, z & 15, id, data);
        // the heightmap only SUBIA to the poner a block; to the quitarlo (or to the
        // cavar a habitacion) the columna is quedaba marcada as
        // techada to the altura vieja, and the semilla of luz of sky
        // tomaba air abierto by tierra solida -> everything negro.
        mcp_chunk_recalculate_height_map_column(c, x & 15, z & 15);
        c->dirty = true;
        // LuZ: before the block is escribia and already, without recalcular nothing.
        // therefore poner a techo dejaba the habitacion to pleno dia and
        // romperlo the dejaba to oscuras. the radio cubre the 15 blocks
        // that the luz can recorrer, this way that the region recalculada
        // always is coherente by if sola.
        light_.relightAround(x, y, z, LightEngine::kRelightRadius);

        // Falling-block support check (Infinity Fallable::onUpdate,
        // BLOCK_UPDATE_NORMAL). setBlock is the ONE choke point every
        // block-modifying system goes through (redstone, explosions,
        // flint&steel, the Lua plugin API world.setBlock(...)), so the
        // support loss is re-evaluated here for every path:
        //   * the removed cell (air now) — the block ABOVE may fall;
        //   * the written cell — a fallable block written over air or a
        //     liquid (plugin placed sand mid-air) falls immediately.
        // The direct chunk-write paths (player break/place) bypass
        // setBlock and call checkFallingSupport() from their own call
        // sites — see PlayerBlocks.cpp.
        if (y >= 0 && y < 256) {
            if (id == block::AIR) {
                checkFallingSupport(x, y + 1, z);
            } else if (fallableBlockId(id)) {
                checkFallingSupport(x, y, z);
            }
        }
    }
}

void World::onChunkAdded(int32_t cx, int32_t cz) {
    // only encola: this function is flame from the thread of the generador
    // (inside of ChunkManager::setChunk) and recorrer the world ahi
    // bloquearia the generation.
    light_.onChunkAdded(cx, cz);
}

int World::save() {
    // Portal registry: tiny text file, rewritten on every full save.
    portals_.save(portals_path_);
    // Write every dirty tile's NBT back into its chunk's tile_entities
    // array BEFORE flushing — flushDirty() is what actually serializes
    // each dirty chunk (tile_entities included) to the .mca region file,
    // so this has to happen first or the chunk gets saved with whatever
    // tile_entities blobs it already had (stale or empty).
    syncTilesToChunks();
    // Delegate to ChunkManager which calls provider_->saveChunk() for each
    // dirty chunk. With the async Anvil writer that call only encodes +
    // enqueues, so flush the writer queue here: save-all and shutdown must
    // not return before every accepted chunk is really on disk. The live
    // autosave (saveBudget) never blocks like this — that's what keeps its
    // per-tick cost in the sub-millisecond range.
    int saved = chunks_.flushDirty();
    if (provider_) provider_->flushPendingSaves();
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

int World::saveBudget(size_t max_chunks, uint64_t max_ms, bool write_level_data) {
    // same order that save(): the tiles to its chunks before serializar,
    // or the chunks is escriben with blobs viejos/vacios.
    syncTilesToChunks();
    int saved = chunks_.flushDirtyBudget(max_chunks, max_ms);
    if (write_level_data && provider_) {
        provider_->setSpawn(math::Vector3{
            (double)spawn_.x, (double)spawn_.y, (double)spawn_.z});
        provider_->setCurrentTime(cfg_.time);
        provider_->saveLevelData();
    }
    return saved;
}

size_t World::unsavedChunkCount() const {
    return chunks_.dirtyCount();
}

void World::tick(uint64_t current_tick) {
    advanceTime(1);
    doWeatherTick(current_tick);
    // Sleep vote (Infinity Level.php:817 `if($this->sleepTicks > 0 and
    // --$this->sleepTicks <= 0){ $this->checkSleep(); }`): after 60 ticks
    // (3 s) with everyone in bed, the night is skipped.
    if (sleep_ticks_ > 0 && --sleep_ticks_ <= 0) {
        checkSleep();
    }
    // Luz pending of the chunks recien generados/loaded. with a
    // presupuesto per tick for not congelar the thread main when
    // entra a vista llena of chunks to the time.
    light_.processPending(LightEngine::kPendingBudget);
    // NOTE: the dropped-items *reclamation* sweep used to run here every
    // tick. It was moved to collectDroppedItems(), driven by the single
    // global GC in MemoryManager — running an independent reclaimer inside
    // tick() freed records out from under player pickup handlers
    // (taken-flag race → phantom pickups) and was one of the scattered GCs
    // this codebase had. The per-tick *physics* stays here: the client
    // integrates the motion we shipped in AddItemEntityPacket every tick,
    // so the server has to do the same or the two drift apart.
    tickDroppedItems(current_tick);
    // Arrow projectiles: same story as the dropped items — the client
    // integrates the speed shipped in AddEntityPacket, so the server must
    // integrate the identical physics (Infinity Arrow gravity) and correct
    // the clients with MoveEntityPacket, or the two drift apart.
    tickArrows(current_tick);
    // Falling blocks (sand / gravel / anvil — Infinity FallingSand): the
    // support-loss conversion happens in checkFallingSupport() from the
    // block-change call sites; this pass runs the actual fall physics
    // and the landing conversion, server-authoritative like everything
    // else in this tick.
    tickFallingBlocks(current_tick);
}

// ============================================================
// World::checkSleep — Infinity Level::checkSleep() (Level.php:861)
// ============================================================
// Runs from tick() when the sleep countdown reaches zero:
//   - No players → nothing (Level.php:864 returns early).
//   - ANY player of this level not sleeping → the night continues
//     (resetTime=false; the "solo duermo si todos duermen" rule the user
//     asked for — with a single player online it is trivially satisfied).
//   - Everyone sleeping AND it is night (TIME_NIGHT 14000 .. TIME_SUNRISE
//     23000) → time jumps to the next morning:
//     Infinity: setTime($t + TIME_FULL - $time%TIME_FULL) — for any
//     in-night $t this lands on the next multiple of 24000, which the
//     0.15.10 client renders as sunrise. Everyone is woken up right
//     after, and the new time is pushed to the clients of THIS world
//     (Infinity syncs the clock via the next SetTimePacket; we send it
//     explicitly like TimeCommand does).
void World::checkSleep() {
    auto& srv = cursestone::Server::getInstance();
    auto players = srv.playersSnapshot();
    if (players.empty()) return;

    bool resetTime = true;
    for (auto& p : players) {
        if (!p || p->isPendingRemoval()) continue;      // quitting players don't count
        if (p->getWorld() != this) continue;            // other levels don't vote here
        if (!p->isSleeping()) { resetTime = false; break; }
    }

    if (!resetTime) return;

    int32_t time = cfg_.time % TIME_FULL;
    if (time >= TIME_NIGHT && time < TIME_SUNRISE) {
        // Jump to the next morning (next multiple of TIME_FULL).
        cfg_.time = cfg_.time + TIME_FULL - time;

        // Wake everyone up (their stopSleep resets sleep_ticks_ to 0 and
        // clears the sleeping metadata + sends the WAKE_UP animation).
        for (auto& p : players) {
            if (!p) continue;
            if (p->getWorld() != this) continue;
            if (p->isSleeping()) p->stopSleep();
        }

        // Push the new clock to every player of this world so the sky
        // visibly turns to sunrise (same pattern TimeCommand uses).
        auto pk = std::make_shared<network::protocol::SetTimePacket>();
        pk->time = cfg_.time;
        pk->started = true;
        for (auto& p : players) {
            if (!p || p->getWorld() != this) continue;
            p->sendPacket(pk);
        }

        utils::MainLogger::get().info("[SLEEP] Everyone is asleep in \"" +
            cfg_.name + "\" — time skipped to morning (" +
            std::to_string(cfg_.time % TIME_FULL) + ")");
    }
}

// ============================================================
// Dropped-item physics — pocketmine\entity\item::onUpdate() (Genisys 0.15)
// ============================================================
//
// Genisys item constants: $gravity = 0.04, $drag = 0.02, width/length/height
// = 0.25. One tick is:
//
//     $this->age++;
//     $this->pickupDelay -= $tickDiff;
//     $this->motionY -= $this->gravity;
//     if($this->checkObstruction($this->x, $this->and, $this->z)) ...
//     $this->move($this->motionX, $this->motionY, $this->motionZ);
//     $friction = 1 - $this->drag;
//     if($this->onGround and (|mx| > 0.00001 or |mz| > 0.00001))
//         $friction = blockBelow->getFrictionFactor() * $friction;   // 0.6
//     $this->motionX *= $friction; $this->motionY *= 1 - $drag; $this->motionZ *= $friction;
//     if($this->onGround) $this->motionY *= -0.5;
//     if($currentTick % 5 == 0) $this->updateMovement();
//
// and Entity::move() resolves the delta one axis at to time against the
// World's collision cubes (and, then X, then Z), zeroing each motion component
// that got clipped.
namespace {

constexpr double kItemGravity  = 0.04;   // pocketmine\entity\item::$gravity
constexpr double kItemDrag     = 0.02;   // pocketmine\entity\item::$drag
constexpr double kBlockFriction = 0.6;   // Block::getFrictionFactor()
// The item box (kItemHalfW / kItemHeight) and the whole pickup volume live
// in World.h so the attraction gate below and the pickup scan in
// player::onUpdate() share ONE definition of "is this item in reach".

// pocketmine\block\Block::$solid — see the forward declaration above.
bool dropBlockSolid(uint8_t id) {
    switch (id) {
        case block::AIR:
        case block::FLOWING_WATER: case block::WATER:
        case block::FLOWING_LAVA:  case block::LAVA:
        case block::FIRE:
        case block::COBWEB:
        case block::SAPLING:      case block::TALL_GRASS:
        case block::DEAD_BUSH:     case block::FLOWER_YELLOW:
        case block::FLOWER_RED:   case block::BROWN_MUSHROOM:
        case block::RED_MUSHROOM: case block::TORCH:
        case block::REDSTONE_TORCH_ON: case block::REDSTONE_WIRE:
        case block::LEVER:        case block::RAIL:
        case block::POWERED_RAIL: case block::DETECTOR_RAIL:
        case block::WHITE_CARPET: case block::SNOW_LAYER:
        case block::LILY_PAD:     case block::VINE:
        case block::SUGARCANE:
        case block::NETHER_PORTAL:
            return false;
        default:
            return true;
    }
}

// ---------------------------------------------------------------------
// Falling-block helpers (protocol 84 / Infinity FallingSand).
// ---------------------------------------------------------------------

// The complete set of blocks that obey gravity in MCPE 0.15.10:
//   sand 12 (incl. red sand 12:1), gravel 13, anvil 145 
// learn them. See the FallingBlockEntity note in World.h.
bool fallableBlockId(uint8_t id) {
    switch (id) {
        case block::SAND:      // 12 — also red sand via meta 1
        case block::GRAVEL:    // 13
        case block::ANVIL:     // 145 — direction bits 0x03, damage 0x0c
            return true;
        default:
            return false;
    }
}

// Infinity Fallable::onUpdate: the block falls when its support is AIR
// `$down->getId() === self::AIR` or a Liquid (`$down instanceof Liquid`
// — all four moving/static water/lava ids).
bool liquidForFalling(uint8_t id) {
    switch (id) {
        case block::FLOWING_WATER: case block::WATER:
        case block::FLOWING_LAVA:  case block::LAVA:
            return true;
        default:
            return false;
    }
}

// Broadcast one UpdateBlockPacket(id,data) for cell (x,y,z) to every
// spawned player of the world that can see the chunk — the same
// visibility rule the break/place paths use (canSeeBlock, NOT
// isTrackingChunk; see the ghost-block note in PlayerBlocks.cpp).
void sendUpdateBlock(World* w, int32_t x, int32_t y, int32_t z,
                     uint8_t id, uint8_t data) {
    auto pk = std::make_shared<network::protocol::UpdateBlockPacket>();
    pk->x = x;
    pk->z = z;
    pk->y = (uint8_t)y;
    pk->blockId = id;
    pk->blockData = data;
    pk->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
    int32_t cx = x >> 4, cz = z >> 4;
    for (auto& p : cursestone::Server::getInstance().playersSnapshot()) {
        if (!p || !p->spawned()) continue;
        if (p->getWorld() != w) continue;   // same world
        if (!p->canSeeBlock(cx, cz)) continue;
        p->sendPacket(pk);
    }
}

// Broadcast any packet to every spawned player of the world that can
// see chunk (cx,cz) — shared by the falling-block spawn/move/remove
// broadcasts so they all stay on ONE visibility rule.
void sendToChunkViewers(World* w, int32_t cx, int32_t cz,
                        const std::shared_ptr<network::protocol::DataPacket>& pk) {
    for (auto& p : cursestone::Server::getInstance().playersSnapshot()) {
        if (!p || !p->spawned()) continue;
        if (p->getWorld() != w) continue;   // same world
        if (!p->canSeeBlock(cx, cz)) continue;
        p->sendPacket(pk);
    }
}

// RemoveEntityPacket broadcast for a despawning falling block — sent to
// every spawned player of the world (same pattern as the arrow despawn;
// clients safely ignore removals of entities they never saw).
void sendRemoveEntity(World* w, int64_t eid) {
    auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
    rm->eid = eid;
    for (auto& p : cursestone::Server::getInstance().playersSnapshot()) {
        if (!p || !p->spawned()) continue;
        if (p->getWorld() != w) continue;
        p->sendPacket(rm);
    }
}

// Anvil landing extras (Infinity FallingSand, `$ev->getTo() instanceof
// Anvil` branch):
//   * AnvilFallSound — LevelEventPacket evid 1022
//     (EVENT_SOUND_ANVIL_FALL on protocol 84) at the block centre;
//   * crush damage `($fallDistance - 1) * 2` capped at 40 to every
//     Living overlapping the landed box grown by 0.1. CurseStone has no
//     mob spawn system yet (players are the only Living in worlds), so
//     the scan covers players; `damage(cause FALL)` is the same
//     EntityDamageEvent cause Infinity's EntityDamageByEntityEvent
//     carries (CAUSE_FALL), minus the attacker that a falling block
//     cannot be anyway.
void anvilLandingEffects(World* w, int32_t bx, int32_t by, int32_t bz,
                         float fall_distance) {
    auto snd = std::make_shared<network::protocol::LevelEventPacket>();
    snd->evid = 1022;   // EVENT_SOUND_ANVIL_FALL (protocol 84)
    snd->x = (float)bx + 0.5f;
    snd->y = (float)by + 0.5f;
    snd->z = (float)bz + 0.5f;
    snd->data = 0;
    sendToChunkViewers(w, bx >> 4, bz >> 4, snd);

    float dmg = (fall_distance - 1.0f) * 2.0f;
    if (dmg > 40.0f) dmg = 40.0f;
    if (dmg <= 0.0f) return;

    // Landed falling-block box (0.98 wide) grown 0.1 on every axis —
    // Infinity `boundingBox->grow(0.1, 0.1, 0.1)`.
    double minx = (double)bx + 0.5 - 0.49 - 0.1, maxx = (double)bx + 0.5 + 0.49 + 0.1;
    double miny = (double)by - 0.1,             maxy = (double)by + 0.98 + 0.1;
    double minz = (double)bz + 0.5 - 0.49 - 0.1, maxz = (double)bz + 0.5 + 0.49 + 0.1;
    for (auto& p : cursestone::Server::getInstance().playersSnapshot()) {
        if (!p || !p->spawned() || p->isDead() || p->isPendingRemoval()) continue;
        if (p->getWorld() != w) continue;
        const math::Vector3& pp = p->getNextPosition();
        // Player AABB: [x±0.3, y .. y+1.8, z±0.3] (Human dimensions).
        if (pp.x + 0.3 <= minx || pp.x - 0.3 >= maxx) continue;
        if (pp.y + 1.8 <= miny || pp.y >= maxy) continue;
        if (pp.z + 0.3 <= minz || pp.z - 0.3 >= maxz) continue;
        p->damage(dmg, event::entity::DamageCause::FALL);
    }
}

} // namespace

void World::tickDroppedItems(uint64_t current_tick) {
    // Snapshot the raw pointers like droppedItems() does — the physics
    // pass never removes records (that is collectDroppedItems' job), it
    // only rewrites the fields in place, and it holds not lock while it
    // sends packets (lock-ordering rule used by every other World method).
    std::vector<DroppedItem*> snapshot;
    {
        std::lock_guard<std::mutex> lk(dropped_items_mtx_);
        snapshot.reserve(dropped_items_.size());
        for (auto& [_, di] : dropped_items_) {
            if (di && !di->taken) snapshot.push_back(di.get());
        }
    }
    if (snapshot.empty()) return;

    // The per-item step below not longer needs the tick number: the sync
    // happens on the tick the item actually moves, so the client is
    // corrected on the very tick the position changes.
    (void)current_tick;

    for (DroppedItem* di : snapshot) {
        // not physics for an item whose chunk is not loaded (getBlockId
        // answers AIR for those, which would let the item sink through an
        // unloaded world forever). It resumes as soon as the chunk is back.
        int item_cx = (int)std::floor(di->x) >> 4;
        int item_cz = (int)std::floor(di->z) >> 4;
        if (chunks_.getChunkIfLoaded(item_cx, item_cz) == nullptr) continue;
        if (di->y < 0.0 || di->y > 255.0) continue;

        // $this->pickupDelay -= $tickDiff (never above 32767 = infinite).
        if (di->pickup_delay_ticks > 0 && di->pickup_delay_ticks < 32767) {
            --di->pickup_delay_ticks;
        }

        // $this->motionY -= $this->gravity;  (BEFORE the move, as in
        // Genisys item::onUpdate — otherwise the item lands one tick late).
        di->vy -= kItemGravity;

        // Entity::checkObstruction() — if the item ended up embedded in to
        // solid block, kick it out along the nearest face.
        {
            int i = (int)std::floor(di->x);
            int j = (int)std::floor(di->y);
            int k = (int)std::floor(di->z);
            double diffX = di->x - i, diffY = di->y - j, diffZ = di->z - k;
            if (dropBlockSolid(getBlockId(i, j, k))) {
                bool f0 = !dropBlockSolid(getBlockId(i - 1, j, k));
                bool f1 = !dropBlockSolid(getBlockId(i + 1, j, k));
                bool f2 = !dropBlockSolid(getBlockId(i, j - 1, k));
                bool f3 = !dropBlockSolid(getBlockId(i, j + 1, k));
                bool f4 = !dropBlockSolid(getBlockId(i, j, k - 1));
                bool f5 = !dropBlockSolid(getBlockId(i, j, k + 1));

                int direction = -1;
                double limit = 9999;
                if (f0)                       { limit = diffX;        direction = 0; }
                if (f1 && 1 - diffX < limit)  { limit = 1 - diffX;    direction = 1; }
                if (f2 && diffY < limit)      { limit = diffY;        direction = 2; }
                if (f3 && 1 - diffY < limit)  { limit = 1 - diffY;    direction = 3; }
                if (f4 && diffZ < limit)      { limit = diffZ;        direction = 4; }
                if (f5 && 1 - diffZ < limit)  {                      direction = 5; }

                double force = (double)std::rand() / (double)RAND_MAX * 0.2 + 0.1;
                switch (direction) {
                    case 0: di->vx = -force; break;
                    case 1: di->vx =  force; break;
                    case 2: di->vy = -force; break;
                    case 3: di->vy =  force; break;
                    case 4: di->vz = -force; break;
                    case 5: di->vz =  force; break;
                    default: break;
                }
            }
        }

        // ---- Vanilla item attraction --------------------------------------
        // An item LYING ON THE GROUND that is outside to player's pickup
        // volume but within kAttractRadius is pulled in. This is
        // Minecraft's own behaviour (items slide to you when you walk up to
        // them) and it is what makes to PILE collect as to pile: the items
        // land spread over more than the 2-block pickup diameter, so
        // standing in the middle of them only ever reached one.
        //
        // Gated on on_ground on purpose: without it the magnet grabs the
        // item mid-flight and it never visibly leaves your hand, so you
        // would never see the toss at all.
        //
        // Genisys 0.15 has not attraction, and its checkNearEntities()
        // tests the player's CACHED boundingBox, which player::__construct
        // initialises to the degenerate AxisAlignedBB(0,0,0,0,0,0) and
        // nothing ever updates (the only writer sits inside to commented-out
        // block in processMovement). So in that fork the pickup volume
        // lives at the world origin and to player can never match an item at
        // all. The gate below therefore uses the vanilla Bedrock volume
        // (itemWithinPickupVolume, World.h) anchored to the actual position —
        // the SAME one player::onUpdate() collects with.
        if (di->on_ground) {
            constexpr double kAttractRadius = 2.0;
            constexpr double kAttractSpeed  = 0.35;
            // One attractor per tick: the CLOSEST player. Taking the first
            // one in the player list instead meant to bystander standing
            // further away could steal the item while the player walking
            // up to it watched it slide the wrong way.
            double best_dist = kAttractRadius;
            double bdx = 0.0, bdy = 0.0, bdz = 0.0;
            bool attracted = false;
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (!p || !p->spawned() || p->isDead()) continue;
                if (p->getWorld() != this) continue;   // same world
                math::Vector3 pp = p->getNextPosition();
                // Already collectable from here -> don't drag it through the
                // floor, the pickup pass this same tick will take it.
                if (itemWithinPickupVolume(*di, pp.x, pp.y, pp.z)) continue;
                double adx = pp.x - di->x;
                double ady = (pp.y + 0.9) - di->y;   // converge at the chest
                double adz = pp.z - di->z;
                double dist = std::sqrt(adx * adx + ady * ady + adz * adz);
                if (dist <= 0.0001 || dist >= best_dist) continue;
                best_dist = dist;
                bdx = adx; bdy = ady; bdz = adz;
                attracted = true;
            }
            if (attracted) {
                double pull = 0.12 * (1.0 - best_dist / kAttractRadius) + 0.03;
                di->vx += bdx / best_dist * pull;
                di->vy += bdy / best_dist * pull;
                di->vz += bdz / best_dist * pull;
                double sp = std::sqrt(di->vx * di->vx + di->vy * di->vy + di->vz * di->vz);
                if (sp > kAttractSpeed) {
                    double k = kAttractSpeed / sp;
                    di->vx *= k; di->vy *= k; di->vz *= k;
                }
            }
        }

        // Entity::move() + Entity::checkGroundState(), now resolved by the
        // shared collision engine (src/physics/Physics.h) instead of by
        // three hand-copied clip loops: the level hands over the collision
        // boxes of the lookup region and physics::sweep() resolves the
        // motion and, then X, then Z, exactly as the client does.
        const double movX = di->vx, movY = di->vy, movZ = di->vz;
        {
            mcp_aabb_t bb = mcp_aabb_make(di->x - kItemHalfW, di->y,              di->z - kItemHalfW,
                                          di->x + kItemHalfW, di->y + kItemHeight, di->z + kItemHalfW);

            // World::getCollisionCubes($this, $bb->offset($dx, $dy, $dz)) —
            // the lookup region is the TRANSLATED box, which is what this
            // integrator has always used (see mcp_aabb_expand() for the
            // swept-volume variant).
            mcp_aabb_t lookup = mcp_aabb_clone_move(bb, movX, movY, movZ);
            const std::vector<physics::BlockBox> boxes = collisionBoxes(lookup);

            physics::MotionResult res = physics::sweep(bb, mcp_v3(movX, movY, movZ), boxes);
            di->x += res.motion.x;
            di->y += res.motion.y;
            di->z += res.motion.z;

            // Entity::checkGroundState(): on the floor only when the
            // downward motion was clipped — the same rule sweep() applies
            // (clipped_y && movY < 0).
            di->on_ground = res.on_ground;
            if (res.clipped_x) di->vx = 0;
            if (res.clipped_y) di->vy = 0;
            if (res.clipped_z) di->vz = 0;
        }

        double friction = 1.0 - kItemDrag;
        if (di->on_ground && (std::fabs(di->vx) > 0.00001 ||
                             std::fabs(di->vz) > 0.00001)) {
            friction = kBlockFriction * friction;
        }
        di->vx *= friction;
        di->vy *= 1.0 - kItemDrag;
        di->vz *= friction;
        if (di->on_ground) di->vy *= -0.5;
    }

    // server-authoritative sync. Genisys only re-broadcasts every 5th tick
    // and only past to 0.2-block dead-zone, which lets the client keep
    // rendering an item up to 0.2 blocks away from where the pickup test
    // actually runs — that gap is the "I am standing right on it and it
    // does not get picked up" report. The item has to land where the server
    // says it is, so the correction goes out on every tick it moves.
    for (DroppedItem* di : snapshot) {
        double dx = di->x - di->lx, dy = di->y - di->ly, dz = di->z - di->lz;
        if (std::fabs(dx) < 0.0005 && std::fabs(dy) < 0.0005 &&
            std::fabs(dz) < 0.0005) continue;
        di->lx = di->x; di->ly = di->y; di->lz = di->z;

        auto pk = std::make_shared<network::protocol::MoveEntityPacket>();
        pk->eid = di->eid;
        pk->x   = (float)di->x;
        pk->y   = (float)di->y;
        pk->z   = (float)di->z;
        int32_t pk_cx = (int32_t)std::floor(di->x) >> 4;
        int32_t pk_cz = (int32_t)std::floor(di->z) >> 4;
        for (auto& p : Server::getInstance().playersSnapshot()) {
            if (!p || !p->spawned()) continue;
            if (p->getWorld() != this) continue;   // same world
            if (!p->canSeeBlock(pk_cx, pk_cz)) continue;
            p->sendPacket(pk);
        }
    }
}

size_t World::collectDroppedItems(uint64_t current_tick) {
    constexpr uint64_t DESPAWN_TICKS = 6000;  // 5 min at 20 TPS — vanilla EntityItem lifetime

    // Phase 1: despawn expired items. Snapshot FIRST, then broadcast with
    // not registry lock held (lock-ordering rule used by every other
    // World method that sends packets). This pass only ever runs on the
    // main thread between player updates (it is driven by the global GC in
    // server::tick), so it can never interleave with to pickup handler that
    // holds to DroppedItem* snapshot. Additionally, if to pickup transaction
    // straddles to pass boundary, player::onUpdate() re-checks the claim via
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
        // One packet per eid — RemoveEntityPacket carries to single eid.
        for (size_t i = 0; i < expired.size(); ++i) {
            if (i > 0) rm = std::make_shared<network::protocol::RemoveEntityPacket>();
            rm->eid = expired[i];
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (p && p->spawned() && p->getWorld() == this) p->sendPacket(rm);
            }
        }
    }

    // Phase 2: drop fully-picked-up entries (the old tick() sweep job).
    // Safe: by the time to record is `taken`, the picking player has already
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

// ============================================================
// Arrow projectiles — pocketmine\entity\Projectile::onUpdate() +
// Arrow::onUpdate() (Infinity, protocol 84). One tick of a flying arrow:
//
//     if (!$this->isCollided) $this->motionY -= $this->gravity;   // 0.05
//     <entity raycast — first player the flight segment crosses>
//     $this->move($motionX, $motionY, $motionZ);                  // blocks
//     if (collided && !hadCollision) { hadCollision = true; motion = 0; }
//     <yaw/pitch from motion>
//     <updateMovement() — MoveEntityPacket when it really moved>
//     if ($this->age > 1200) $this->kill();
//
// Entity hit damage (Projectile::onUpdate): $damage = ceil(speed * base),
// + mt_rand(0, damage/2 + 1) for critical (fully charged) arrows.
// ============================================================
void World::addArrow(ArrowEntity a) {
    std::lock_guard<std::mutex> lk(arrows_mtx_);
    arrows_[a.eid] = std::make_unique<ArrowEntity>(std::move(a));
}

std::vector<ArrowEntity*> World::arrows() {
    std::vector<ArrowEntity*> out;
    std::lock_guard<std::mutex> lk(arrows_mtx_);
    out.reserve(arrows_.size());
    for (auto& [_, a] : arrows_) {
        if (a && !a->taken) out.push_back(a.get());
    }
    return out;
}

bool World::tryClaimArrow(int64_t eid) {
    std::lock_guard<std::mutex> lk(arrows_mtx_);
    auto it = arrows_.find(eid);
    if (it == arrows_.end() || !it->second) return false;
    if (it->second->taken) return false;
    it->second->taken = true;
    return true;
}

void World::tickArrows(uint64_t current_tick) {
    // Snapshot the raw pointers like tickDroppedItems does — the physics
    // pass holds no lock while it sends packets (lock-ordering rule).
    std::vector<ArrowEntity*> snapshot;
    {
        std::lock_guard<std::mutex> lk(arrows_mtx_);
        snapshot.reserve(arrows_.size());
        for (auto& [_, a] : arrows_) {
            if (a && !a->taken) snapshot.push_back(a.get());
        }
    }
    if (snapshot.empty()) return;

    for (ArrowEntity* a : snapshot) {
        // PMMP Arrow::onUpdate — despawn after 1200 ticks in flight OR
        // stuck (60 s at 20 tps).
        if (current_tick - a->spawn_tick > 1200) {
            a->taken = true;
            auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
            rm->eid = a->eid;
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (p && p->spawned() && p->getWorld() == this) p->sendPacket(rm);
            }
            continue;
        }

        // Stuck arrows freeze (motion is already zero) — the pickup scan
        // is player-side. Still counted so their age keeps running.
        if (a->stuck) { ++a->flight_ticks; continue; }

        // No physics while the chunk is not resident — getBlockId answers
        // AIR for unloaded chunks and the arrow would fall through the
        // world (same gate as tickDroppedItems).
        int acx = (int)std::floor(a->x) >> 4;
        int acz = (int)std::floor(a->z) >> 4;
        if (chunks_.getChunkIfLoaded(acx, acz) == nullptr) continue;
        ++a->flight_ticks;

        // $this->motionY -= $this->gravity; (Infinity Arrow: 0.05)
        a->vy -= 0.05;

        double nx = a->x + a->vx;
        double ny = a->y + a->vy;
        double nz = a->z + a->vz;

        // ---- Entity raycast: first player the flight segment crosses ----
        // PMMP walks getCollidingEntities over the motion-expanded bounding
        // box and keeps the closest intercept against each entity's bounding
        // box grown by 0.3. Equivalent slab test per player, straight from
        // the segment (old pos -> new pos) to the grown AABB.
        Player* hit = nullptr;
        double bestT = 2.0;   // t in [0,1] along the segment
        for (auto& p : Server::getInstance().playersSnapshot()) {
            if (!p || !p->spawned() || p->isDead()) continue;
            if (p->isPendingRemoval()) continue;
            if (p->getWorld() != this) continue;
            // The shooter cannot be hit by its own arrow for the first
            // 5 ticks (PMMP: $entity === $this->shootingEntity and
            // $this->ticksLived < 5).
            if (p->runtimeId() == a->shooter_eid && a->flight_ticks < 5) continue;
            const auto& pp = p->getNextPosition();
            // Player AABB [x-0.3, y, z-0.3]..[x+0.3, y+1.8, z+0.3], grown 0.3.
            double minx = pp.x - 0.6, maxx = pp.x + 0.6;
            double miny = pp.y - 0.3, maxy = pp.y + 2.1;
            double minz = pp.z - 0.6, maxz = pp.z + 0.6;
            if (nx < minx && a->x < minx) continue;
            if (nx > maxx && a->x > maxx) continue;
            if (ny < miny && a->y < miny) continue;
            if (ny > maxy && a->y > maxy) continue;
            if (nz < minz && a->z < minz) continue;
            if (nz > maxz && a->z > maxz) continue;
            // Slab method: t interval where the segment is inside the box.
            double tmin = 0.0, tmax = 1.0;
            bool miss = false;
            const double so[3] = {a->x, a->y, a->z};
            const double sd[3] = {nx - a->x, ny - a->y, nz - a->z};
            const double blo[3] = {minx, miny, minz};
            const double bhi[3] = {maxx, maxy, maxz};
            for (int ax = 0; ax < 3; ++ax) {
                if (std::fabs(sd[ax]) < 1e-12) {
                    if (so[ax] < blo[ax] || so[ax] > bhi[ax]) { miss = true; break; }
                    continue;
                }
                double t1 = (blo[ax] - so[ax]) / sd[ax];
                double t2 = (bhi[ax] - so[ax]) / sd[ax];
                if (t1 > t2) std::swap(t1, t2);
                if (t1 > tmin) tmin = t1;
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax) { miss = true; break; }
            }
            if (miss || tmin > bestT) continue;
            bestT = tmin;
            hit = p.get();
        }
        if (hit) {
            // $motion = sqrt(motionX^2 + motionY^2 + motionZ^2);
            // $damage = ceil($motion * $this->damage);
            double speed = std::sqrt(a->vx * a->vx + a->vy * a->vy + a->vz * a->vz);
            float dmg = std::ceil(speed * a->damage);
            if (a->critical) {
                // $damage += mt_rand(0, (int)($damage / 2) + 1);
                dmg += (float)(std::rand() % ((int)(dmg / 2) + 2));
            }
            // Resolve the shooter for EntityDamageByEntityEvent; fall back
            // to a plain PROJECTILE damage when it logged off.
            Player* shooter = nullptr;
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (p && p->runtimeId() == a->shooter_eid) { shooter = p.get(); break; }
            }
            // PROJECTILE cause so Lua listeners can tell bow hits from
            // melee (EntityDamageByEntityEvent carries the cause now).
            auto applied = shooter
                ? hit->damageByEntity(dmg, shooter,
                                      event::entity::DamageCause::PROJECTILE)
                : hit->damage(dmg, event::entity::DamageCause::PROJECTILE);

            if (applied != cursestone::entity::DamageApply::CANCELLED) {
                // Hurt animation to the VIEWERS ONLY — same rule as
                // Player::handleInteract (Infinity Living::attack): the
                // packet references the victim's eid and the victim's own
                // client gets its self-referenced eid=0 variant from
                // Player::onHealthChanged instead. Sending the real-eid
                // packet to the victim crashed creative clients. On
                // KILLED, Player::onDeath() already broadcast the full
                // death sequence, so nothing is sent here either.
                if (applied == cursestone::entity::DamageApply::HURT) {
                    auto hurtEv = std::make_shared<network::protocol::EntityEventPacket>();
                    hurtEv->eid = hit->runtimeId();
                    hurtEv->event = network::protocol::EntityEventPacket::HURT_ANIMATION;
                    for (auto& other : Server::getInstance().playersSnapshot()) {
                        if (!other || other.get() == hit) continue;
                        if (!other->spawned()) continue;
                        other->sendPacket(hurtEv);
                    }
                    auto sh = std::make_shared<network::protocol::SetHealthPacket>();
                    sh->health = std::max(0, (int32_t)hit->health());
                    hit->sendPacket(sh);
                }
            }

            // PMMP kills the projectile when it hits an entity. Even a
            // cancelled (creative-immune) hit despawns the arrow: it did
            // physically reach the player.
            a->taken = true;
            auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
            rm->eid = a->eid;
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (p && p->spawned() && p->getWorld() == this) p->sendPacket(rm);
            }
            continue;
        }

        // ---- Move with anti-tunneling substeps (block collision) ----
        // PMMP's move() sweeps the bounding box; with the record model a
        // stepped center-point probe is the equivalent that cannot tunnel
        // through a thin wall at full charge (~3 blocks/tick).
        double maxStep = std::max(std::fabs(a->vx),
                        std::max(std::fabs(a->vy), std::fabs(a->vz)));
        int steps = (int)std::ceil(maxStep / 0.5);
        if (steps < 1) steps = 1;
        if (steps > 8) steps = 8;
        bool collided = false;
        int hitStep = 0;
        for (int i = 1; i <= steps; ++i) {
            double sx = a->x + a->vx * ((double)i / steps);
            double sy = a->y + a->vy * ((double)i / steps);
            double sz = a->z + a->vz * ((double)i / steps);
            if (dropBlockSolid(getBlockId((int)std::floor(sx),
                                          (int)std::floor(sy),
                                          (int)std::floor(sz)))) {
                collided = true;
                hitStep = i;
                break;
            }
        }
        if (collided) {
            // PMMP: hadCollision = true; motion = 0. Bisect the blocked
            // sub-step so the arrow sticks right at the block surface and
            // keeps the orientation it had in flight.
            double tFree = (double)(hitStep - 1) / steps;
            double tHit  = (double)hitStep / steps;
            for (int k = 0; k < 8; ++k) {
                double tm = 0.5 * (tFree + tHit);
                if (dropBlockSolid(getBlockId((int)std::floor(a->x + a->vx * tm),
                                              (int)std::floor(a->y + a->vy * tm),
                                              (int)std::floor(a->z + a->vz * tm)))) {
                    tHit = tm;
                } else {
                    tFree = tm;
                }
            }
            double fh0 = std::sqrt(a->vx * a->vx + a->vz * a->vz);
            a->yaw   = (float)(std::atan2(a->vx, a->vz) * 180.0 / 3.14159265358979323846);
            a->pitch = (float)(std::atan2(a->vy, fh0)    * 180.0 / 3.14159265358979323846);
            a->x += a->vx * tFree;
            a->y += a->vy * tFree;
            a->z += a->vz * tFree;
            a->stuck = true;
            a->vx = a->vy = a->vz = 0.0;
        } else {
            a->x = nx; a->y = ny; a->z = nz;
        }

        // Void / build-limit despawn.
        if (a->y < 0.0 || a->y > 260.0) {
            a->taken = true;
            auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
            rm->eid = a->eid;
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (p && p->spawned() && p->getWorld() == this) p->sendPacket(rm);
            }
            continue;
        }

        // ---- Yaw/pitch from motion (PMMP) ----
        if (!a->stuck) {
            double fh = std::sqrt(a->vx * a->vx + a->vz * a->vz);
            a->yaw   = (float)(std::atan2(a->vx, a->vz) * 180.0 / 3.14159265358979323846);
            a->pitch = (float)(std::atan2(a->vy, fh)     * 180.0 / 3.14159265358979323846);
        }

        // ---- updateMovement(): Infinity Entity::updateMovement() thresholds ----
        auto sq = [](double v) { return v * v; };
        double dpos = sq(a->x - a->lx) + sq(a->y - a->ly) + sq(a->z - a->lz);
        double drot = sq((double)a->yaw - a->lyaw) + sq((double)a->pitch - a->lpitch);
        double dmot = sq(a->vx - a->lmx) + sq(a->vy - a->lmy) + sq(a->vz - a->lmz);
        double vlen2 = a->vx * a->vx + a->vy * a->vy + a->vz * a->vz;

        bool sendMove   = collided || dpos > 0.04 ||
                          (drot > 2.25 && dmot > 0.0001 && vlen2 <= 0.00001);
        bool sendMotion = collided || dmot > 0.0025 ||
                          (dmot > 0.0001 && vlen2 <= 0.0001);
        if (!sendMove && !sendMotion) continue;

        int32_t vcx = (int32_t)std::floor(a->x) >> 4;
        int32_t vcz = (int32_t)std::floor(a->z) >> 4;

        if (sendMove) {
            a->lx = a->x; a->ly = a->y; a->lz = a->z;
            a->lyaw = a->yaw; a->lpitch = a->pitch;

            auto mv = std::make_shared<network::protocol::MoveEntityPacket>();
            mv->eid = a->eid;
            mv->x = (float)a->x;
            mv->y = (float)a->y;
            mv->z = (float)a->z;
            mv->yaw = a->yaw;
            mv->headYaw = a->yaw;
            mv->pitch = a->pitch;
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (!p || !p->spawned() || p->getWorld() != this) continue;
                if (!p->canSeeBlock(vcx, vcz)) continue;
                p->sendPacket(mv);
            }
        }

        if (sendMotion) {
            a->lmx = a->vx; a->lmy = a->vy; a->lmz = a->vz;

            auto mo = std::make_shared<network::protocol::SetEntityMotionPacket>();
            mo->entities.push_back({a->eid, (float)a->vx, (float)a->vy, (float)a->vz});
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (!p || !p->spawned() || p->getWorld() != this) continue;
                if (!p->canSeeBlock(vcx, vcz)) continue;
                p->sendPacket(mo);
            }
        }
    }

    // Phase 2: drop claimed/expired records (same shape as the
    // dropped-items sweep).
    {
        std::lock_guard<std::mutex> lk(arrows_mtx_);
        for (auto it = arrows_.begin(); it != arrows_.end(); ) {
            if (it->second && it->second->taken) it = arrows_.erase(it);
            else ++it;
        }
    }
}

// ============================================================
// Falling-block physics — Infinity FallingSand::onUpdate (protocol 84,
// the MCPE 0.15.10 reference this port follows).
// ============================================================
//
// Infinity tick (pocketmine/entity/FallingSand.php):
//     $this->motionY -= $this->gravity;           // 0.04
//     $this->move($motionX, $motionY, $motionZ);  // swept vs blocks
//     $friction = 1 - $this->drag;                // 0.98
//     motionX *= $friction; motionY *= 1-$drag; motionZ *= $friction;
//     if ($this->onGround):
//         kill; $pos = round(x-0.5, y, z-0.5);
//         if (cell id > 0 && !solid && !liquid) → drop the block as item
//         else                                  → setBlock(id, damage)
//         anvil → AnvilFallSound + (($fallDistance-1)*2, cap 40) crush
//     $this->updateMovement();
//
// The only fallable blocks in MCPE 0.15.10 are sand (12, incl. red
// sand), gravel (13) and the anvil (145) — see fallableBlockId().
//
// Anti-runaway guarantees (task requirement: no infinite falls, no
// leaked records):
//   * every record ages every tick — even one frozen in an unloaded
//     chunk is despawned by the kFallingBlockMaxAge cap, so a record can
//     never outlive the world;
//   * conversion only ever happens through this single pass (main
//     thread), so two entities can never claim the same landing cell;
//   * entities are swept bottom-up so a stacked column lands as a
//     column: the lower block becomes a real block first and the one
//     above clips against it instead of tunneling into its cell (which
//     would turn the whole column into item drops — the classic
//     stacked-sand duplication/loss bug).
namespace {

// 30 s at 20 TPS. A falling block under real gravity always lands far
// earlier than this; only an entity stuck in a never-loaded chunk (or
// some pathological state) can reach the cap. On expiry the entity
// despawns — Infinity's chunk-unload path closes its entities the same
// way, so this is no worse than the reference implementation.
constexpr uint32_t kFallingBlockMaxAge = 600;

} // namespace

bool World::isFallableBlockId(uint8_t id) {
    return fallableBlockId(id);
}

void World::checkFallingSupport(int32_t x, int32_t y, int32_t z) {
    if (y < 0 || y >= 256) return;
    // Cheap probe — never trigger chunk generation from a support check
    // (same rule as setBlock: a missing chunk contributes nothing).
    if (chunks_.getChunkIfLoaded(x >> 4, z >> 4) == nullptr) return;
    uint8_t id = getBlockId(x, y, z);
    if (!fallableBlockId(id)) return;
    // Infinity Fallable::onUpdate (BLOCK_UPDATE_NORMAL): the block falls
    // when the one below is AIR or a Liquid. Below the world floor
    // (y == 0) there is no support either — PMMP answers air for y < 0,
    // so the block falls into the void.
    uint8_t below = (y > 0) ? getBlockId(x, y - 1, z) : (uint8_t)block::AIR;
    if (below != block::AIR && !liquidForFalling(below)) return;
    spawnFallingBlock(x, y, z);
}

void World::spawnFallingBlock(int32_t bx, int32_t by, int32_t bz) {
    if (by < 0 || by >= 256) return;
    if (chunks_.getChunkIfLoaded(bx >> 4, bz >> 4) == nullptr) return;
    // Read from the chunk (and re-validate): the caller checked a moment
    // ago, but this is also a recursion entry point (via the cascade
    // below) — be safe.
    uint8_t cellId = getBlockId(bx, by, bz);
    if (!fallableBlockId(cellId)) return;
    uint8_t cellData = getBlockData(bx, by, bz);

    int64_t eid = cursestone::Server::getInstance().allocateEid();

    // The source cell becomes air. world::setBlock() keeps the heightmap,
    // the light region and the chunk dirty flag correct in one call.
    setBlock(bx, by, bz, 0, 0);
    sendUpdateBlock(this, bx, by, bz, 0, 0);

    FallingBlockEntity fb;
    fb.eid       = eid;
    fb.blockId   = cellId;
    fb.blockData = cellData;
    fb.x = (double)bx + 0.5;
    fb.y = (double)by;
    fb.z = (double)bz + 0.5;
    fb.vx = fb.vy = fb.vz = 0.0;
    fb.lx = fb.x; fb.ly = fb.y; fb.lz = fb.z;
    fb.spawn_tick = cursestone::Server::getInstance().getCurrentTick();
    {
        std::lock_guard<std::mutex> lk(falling_blocks_mtx_);
        falling_blocks_[eid] = std::make_unique<FallingBlockEntity>(std::move(fb));
    }

    // AddEntityPacket type 66 (Infinity FallingSand::NETWORK_ID) with the
    // DATA_BLOCK_INFO (20) int metadata `blockId | (damage << 8)` — the
    // 0.15.10 client renders the falling block from that property
    // (Infinity FallingSand::initEntity / spawnTo).
    auto pk = std::make_shared<network::protocol::AddEntityPacket>();
    pk->eid = eid;
    pk->type = network::protocol::AddEntityPacket::TYPE_FALLING_BLOCK;
    pk->x = (float)(bx + 0.5);
    pk->y = (float)by;
    pk->z = (float)(bz + 0.5);
    pk->speedX = pk->speedY = pk->speedZ = 0.0f;
    pk->yaw = 0.0f; pk->pitch = 0.0f;
    network::protocol::SetEntityDataPacket::Entry meta;
    meta.index = network::protocol::SetEntityDataPacket::PROP_BLOCK_INFO;
    meta.type  = network::protocol::SetEntityDataPacket::TYPE_INT;
    meta.ival  = (int32_t)((uint32_t)cellId | ((uint32_t)cellData << 8));
    pk->metadata.push_back(meta);
    sendToChunkViewers(this, bx >> 4, bz >> 4, pk);

    // NOTE: the stacked-column cascade (the cell above losing its
    // support) is driven by the support check INSIDE World::setBlock —
    // the setBlock(air) above triggers checkFallingSupport(bx, by+1, bz)
    // for us, recursively converting the rest of the column.
}

void World::dropItemAt(double x, double y, double z,
                       uint16_t itemId, uint8_t count, uint16_t damage) {
    if (itemId == 0 || count == 0) return;
    int64_t eid = cursestone::Server::getInstance().allocateEid();

    // World::dropItem()'s default nudge (Genisys
    // `lcg_value()*0.2-0.1, 0.2, lcg_value()*0.2-0.1`): makes the drop
    // pop out of the cell it broke in instead of sinking into it.
    auto lcg = []() { return (double)std::rand() / (double)RAND_MAX; };

    auto pk = std::make_shared<network::protocol::AddItemEntityPacket>();
    pk->eid        = eid;
    pk->itemId     = (int16_t)itemId;
    pk->itemCount  = count;
    pk->itemDamage = (int16_t)damage;
    pk->x = (float)x;
    pk->y = (float)y;
    pk->z = (float)z;
    pk->speedX = (float)(lcg() * 0.2 - 0.1);
    pk->speedY = 0.2f;
    pk->speedZ = (float)(lcg() * 0.2 - 0.1);
    sendToChunkViewers(this, (int32_t)std::floor(x) >> 4,
                       (int32_t)std::floor(z) >> 4, pk);

    // Register the record so the pickup scan and tickDroppedItems see it
    // (same fields Player::dropItem fills for player drops).
    DroppedItem di;
    di.eid        = eid;
    di.itemId     = itemId;
    di.count      = count;
    di.damage     = damage;
    di.x          = x;
    di.y          = y;
    di.z          = z;
    di.spawn_tick = cursestone::Server::getInstance().getCurrentTick();
    di.pickup_delay_ticks = 10;   // World::dropItem() default (0.5 s)
    di.taken      = false;
    di.vx         = pk->speedX;
    di.vy         = pk->speedY;
    di.vz         = pk->speedZ;
    di.lx = x; di.ly = y; di.lz = z;
    {
        std::lock_guard<std::mutex> lk(dropped_items_mtx_);
        dropped_items_[eid] = std::make_unique<DroppedItem>(std::move(di));
    }
}

void World::tickFallingBlocks(uint64_t current_tick) {
    (void)current_tick;   // age is per-record; no global tick dependency

    // Snapshot the raw pointers (lock-ordering rule: never hold a
    // registry mutex while sending packets) SORTED BY Y ASCENDING so a
    // stacked column lands bottom-up — the lower entity becomes a real
    // block in the same tick before the upper one is swept, so the upper
    // one rests on it instead of tunneling into its cell.
    std::vector<FallingBlockEntity*> snapshot;
    {
        std::lock_guard<std::mutex> lk(falling_blocks_mtx_);
        snapshot.reserve(falling_blocks_.size());
        for (auto& [_, fb] : falling_blocks_) {
            if (fb && !fb->taken) snapshot.push_back(fb.get());
        }
    }
    if (snapshot.empty()) return;
    std::sort(snapshot.begin(), snapshot.end(),
              [](const FallingBlockEntity* a, const FallingBlockEntity* b) {
                  if (a->y != b->y) return a->y < b->y;
                  return a->eid < b->eid;   // stable tie-break
              });

    for (FallingBlockEntity* fb : snapshot) {
        // --- Age + runaway cap (runs even for frozen entities) -------
        ++fb->age;
        if (fb->age > kFallingBlockMaxAge) {
            fb->taken = true;
            sendRemoveEntity(this, fb->eid);
            if (cursestone::Server::getInstance().advancedLog()) {
                utils::MainLogger::get().info(
                    "[FALLING] eid=" + std::to_string(fb->eid) +
                    " despawned at age cap (chunk unloaded?) — block lost");
            }
            continue;
        }

        // --- Void / build limit: despawn, nothing is placed ----------
        // Infinity kills a FallingSand in the void via CAUSE_VOID
        // damage; the net effect is the entity vanishing without
        // placing anything, which is exactly this.
        if (fb->y < 0.0 || fb->y > 256.0) {
            fb->taken = true;
            sendRemoveEntity(this, fb->eid);
            continue;
        }

        // --- No physics while the chunk is not resident ---------------
        // getBlockId answers AIR for unloaded chunks — without this gate
        // the block would fall through an unloaded world forever. The
        // age cap above still reaps it eventually.
        int fcx = (int)std::floor(fb->x) >> 4;
        int fcz = (int)std::floor(fb->z) >> 4;
        if (chunks_.getChunkIfLoaded(fcx, fcz) == nullptr) continue;

        // --- Gravity, then the swept move (Infinity order) -----------
        fb->vy -= 0.04;

        // Infinity FallingSand box: width/length/height 0.98, centred on
        // the entity position.
        mcp_aabb_t bb = mcp_aabb_make(fb->x - 0.49, fb->y, fb->z - 0.49,
                                      fb->x + 0.49, fb->y + 0.98, fb->z + 0.49);
        mcp_aabb_t lookup = mcp_aabb_clone_move(bb, fb->vx, fb->vy, fb->vz);
        const std::vector<physics::BlockBox> boxes = collisionBoxes(lookup);

        physics::MotionResult res = physics::sweep(bb, mcp_v3(fb->vx, fb->vy, fb->vz), boxes);
        fb->x += res.motion.x;
        fb->y += res.motion.y;
        fb->z += res.motion.z;

        // fallDistance feeds the anvil crush damage
        // (Infinity FallingSand: `$height = $this->fallDistance`).
        if (res.motion.y < 0.0) fb->fall_distance += (float)(-res.motion.y);

        if (res.clipped_x) fb->vx = 0.0;
        if (res.clipped_z) fb->vz = 0.0;

        // --- Landing (Infinity: `if ($this->onGround)`) ---------------
        if (res.on_ground) {
            fb->taken = true;

            // Landing cell — Infinity `(x-0.5, y, z-0.5)->round()`. The
            // sweep clips EXACTLY at contact, so y is the integer top of
            // the block below and floor(y+0.5) is that top cell.
            int cx = (int)std::floor(fb->x);        // round(x - 0.5), x = cell + 0.5
            int cy = (int)std::floor(fb->y + 0.5);  // round(y)
            int cz = (int)std::floor(fb->z);        // round(z - 0.5), z = cell + 0.5

            // Final absolute position so clients see the entity stop
            // where it landed before the despawn broadcast.
            {
                auto mv = std::make_shared<network::protocol::MoveEntityPacket>();
                mv->eid = fb->eid;
                mv->x = (float)fb->x;
                mv->y = (float)fb->y;
                mv->z = (float)fb->z;
                sendToChunkViewers(this, cx >> 4, cz >> 4, mv);
            }
            sendRemoveEntity(this, fb->eid);

            // --- Convert back into a block ----------------------------
            bool chunkOk = (cy >= 0 && cy < 256) &&
                           (chunks_.getChunkIfLoaded(cx >> 4, cz >> 4) != nullptr);
            if (chunkOk) {
                uint8_t cellId = getBlockId(cx, cy, cz);
                if (cellId == block::SNOW_LAYER) {
                    // Vanilla: the falling block replaces the snow layer
                    // (the layer is destroyed, no drop, the falling
                    // block keeps its OWN metadata — Infinity's
                    // `damage + oldDamage` merge would corrupt e.g. an
                    // anvil's damage bits with the layer's thickness).
                    setBlock(cx, cy, cz, fb->blockId, fb->blockData);
                    sendUpdateBlock(this, cx, cy, cz, fb->blockId, fb->blockData);
                    if (fb->blockId == block::ANVIL) anvilLandingEffects(this, cx, cy, cz, fb->fall_distance);
                } else if (cellId > 0 && !dropBlockSolid(cellId) &&
                           !liquidForFalling(cellId)) {
                    // Infinity: a non-solid, non-liquid obstacle (torch,
                    // tall grass, sapling...) pops the falling block as
                    // an ITEM instead of replacing it.
                    dropItemAt(fb->x, fb->y, fb->z,
                               (uint16_t)fb->blockId, 1, (uint16_t)fb->blockData);
                    if (fb->blockId == block::ANVIL) anvilLandingEffects(this, cx, cy, cz, fb->fall_distance);
                } else {
                    // Air, liquid (the block sinks through and replaces
                    // the liquid cell it stopped in) or anything else —
                    // the block lands here with its metadata intact
                    // (red sand stays red, anvil keeps direction +
                    // damage bits).
                    setBlock(cx, cy, cz, fb->blockId, fb->blockData);
                    sendUpdateBlock(this, cx, cy, cz, fb->blockId, fb->blockData);
                    if (fb->blockId == block::ANVIL) anvilLandingEffects(this, cx, cy, cz, fb->fall_distance);
                }
            }
            // Chunk unloaded at the moment of landing: the entity
            // despawns and the block is lost — the same thing Infinity
            // does (its entities are closed when their chunk unloads).
            continue;
        }

        // --- drag (Infinity: friction = 1 - drag = 0.98 on all axes) --
        fb->vx *= 0.98;
        fb->vy *= 0.98;
        fb->vz *= 0.98;

        // --- server-authoritative movement sync -----------------------
        // Same dead-zone pattern as tickDroppedItems: correct the client
        // on the very tick the position changes, so the falling block
        // never visibly drifts from where the server will place it.
        double dx = fb->x - fb->lx, dy = fb->y - fb->ly, dz = fb->z - fb->lz;
        if (std::fabs(dx) < 0.0005 && std::fabs(dy) < 0.0005 &&
            std::fabs(dz) < 0.0005) continue;
        fb->lx = fb->x; fb->ly = fb->y; fb->lz = fb->z;

        auto mv = std::make_shared<network::protocol::MoveEntityPacket>();
        mv->eid = fb->eid;
        mv->x = (float)fb->x;
        mv->y = (float)fb->y;
        mv->z = (float)fb->z;
        int32_t vcx = (int32_t)std::floor(fb->x) >> 4;
        int32_t vcz = (int32_t)std::floor(fb->z) >> 4;
        sendToChunkViewers(this, vcx, vcz, mv);
    }

    // Phase 2: drop landed/despawned records (same shape as the
    // dropped-items sweep).
    {
        std::lock_guard<std::mutex> lk(falling_blocks_mtx_);
        for (auto it = falling_blocks_.begin(); it != falling_blocks_.end(); ) {
            if (it->second && it->second->taken) it = falling_blocks_.erase(it);
            else ++it;
        }
    }
}

size_t World::tickTiles(uint64_t current_tick) {
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

void World::registerLoader(uint64_t id, class ChunkLoader* loader) {
    loaders_[id] = loader;
}

void World::unregisterLoader(uint64_t id) {
    loaders_.erase(id);
}

// ============================================================
// Dropped-item registry
// ============================================================
DroppedItem* World::droppedItem(int64_t eid) {
    std::lock_guard<std::mutex> lk(dropped_items_mtx_);
    auto it = dropped_items_.find(eid);
    if (it == dropped_items_.end()) return nullptr;
    return it->second.get();
}

std::vector<DroppedItem*> World::droppedItems() {
    std::lock_guard<std::mutex> lk(dropped_items_mtx_);
    std::vector<DroppedItem*> out;
    out.reserve(dropped_items_.size());
    for (auto& [_, di] : dropped_items_) {
        if (di && !di->taken) out.push_back(di.get());
    }
    return out;
}

void World::addDroppedItem(const DroppedItem& di) {
    std::lock_guard<std::mutex> lk(dropped_items_mtx_);
    auto copy = std::make_unique<DroppedItem>(di);
    dropped_items_[di.eid] = std::move(copy);
}

void World::removeDroppedItem(int64_t eid) {
    std::lock_guard<std::mutex> lk(dropped_items_mtx_);
    auto it = dropped_items_.find(eid);
    if (it != dropped_items_.end()) {
        it->second->taken = true;
    }
}

bool World::tryClaimDroppedItem(int64_t eid) {
    std::lock_guard<std::mutex> lk(dropped_items_mtx_);
    auto it = dropped_items_.find(eid);
    if (it == dropped_items_.end()) return false;
    DroppedItem* di = it->second.get();
    if (!di || di->taken) return false;
    // Atomically claim: set taken=true while holding the lock.
    // This prevents to second player from also claiming the same item.
    di->taken = true;
    return true;
}

// ============================================================
// tile-entity registry
// ============================================================
std::shared_ptr<cursestone::tile::Tile> World::getTile(int32_t x, int32_t y, int32_t z) {
    ensureTilesLoaded(x >> 4, z >> 4);
    std::lock_guard<std::mutex> lk(tiles_mtx_);
    auto it = tiles_.find(packTilePos(x, y, z));
    if (it == tiles_.end()) return nullptr;
    return it->second;
}

void World::setTile(int32_t x, int32_t y, int32_t z, std::shared_ptr<cursestone::tile::Tile> t) {
    std::lock_guard<std::mutex> lk(tiles_mtx_);
    tiles_[packTilePos(x, y, z)] = std::move(t);
}

void World::removeTile(int32_t x, int32_t y, int32_t z) {
    std::lock_guard<std::mutex> lk(tiles_mtx_);
    auto it = tiles_.find(packTilePos(x, y, z));
    if (it == tiles_.end()) return;
    tiles_.erase(it);
    // The NBT blob of the tile we just dropped is still sitting in the
    // owning chunk's tile_entities array. syncTilesToChunks() rebuilds to
    // chunk from the tiles that are STILL LIVE, so to chunk whose only tile
    // was just broken would never be visited and the stale blob would
    // survive to disk — the sign/chest would come back after to restart.
    // Mark the chunk so the next sync clears and rewrites it even though
    // it not longer has any tile.
    tiles_dirty_chunks_.insert(packChunkPos(x >> 4, z >> 4));
}

std::vector<std::shared_ptr<cursestone::tile::Tile>> World::getTilesInChunk(int32_t cx, int32_t cz) {
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
// tile-entity persistence (NBT <-> mcp_chunk_t::tile_entities)
// ============================================================
void World::ensureTilesLoaded(int32_t cx, int32_t cz) {
    uint64_t ckey = packChunkPos(cx, cz);
    {
        std::lock_guard<std::mutex> lk(tiles_mtx_);
        if (tiles_hydrated_chunks_.count(ckey)) return;
    }
    mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
    // "Not loaded yet" is NOT the same as "already looked at":
    // getChunkIfLoaded never generates, so to chunk that is missing here
    // still has to be scanned once it exists. Claiming it now would burn
    // that chunk's only hydration pass and bring back the invisible-chest
    // bug for every container reached that way. Claim it only now, with
    // the chunk actually in hand — to chunk with zero containers is still
    // marked, so the scan runs once and not on every getTile().
    if (!c) return;
    {
        std::lock_guard<std::mutex> lk(tiles_mtx_);
        if (tiles_hydrated_chunks_.count(ckey)) return;  // another thread won
        tiles_hydrated_chunks_.insert(ckey);
    }

    if (c->tile_entities && c->tile_entity_count > 0) {
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
            cursestone::world::Position pos(tx, ty, tz);
            if (std::strcmp(id, "Chest") == 0) {
                tile = std::make_shared<cursestone::tile::TileChest>(pos);
            } else if (std::strcmp(id, "Sign") == 0) {
                tile = std::make_shared<cursestone::tile::TileSign>(pos);
            } else if (std::strcmp(id, "Furnace") == 0) {
                // the furnace now persiste contenido actual (3 slots + burn/
                // cook ticks — ver TileFurnace::writeNBT), this way that is
                // reconstruye just like the chest.
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

    hydrateTilesFromBlocks(cx, cz, c);
}

// hydrateTilesFromBlocks — give every tile-bearing BLOCK in the chunk to
// tile that does not have one yet.
//
// This is the fix for "village chests are invisible and cannot be
// opened": to chest placed by the generator (or loaded from to world that
// never had one) exists only as to block. In MCPE 0.15 the client draws
// and opens chests/furnaces through their block actor, so without this
// pass the player sees an unknown-but-solid block, tapping it does
// nothing, and to furnace takes the client down with it. PMMP creates
// these tiles when the chunk is initialised, for exactly this reason.
//
// Only positions without to tile are filled, so this never overwrites to
// chest the player filled or to furnace that is burning.
void World::hydrateTilesFromBlocks(int32_t cx, int32_t cz, mcp_chunk_t* c) {
    using cursestone::tile::createTileForBlock;
    using cursestone::tile::isTileBlock;

    const int32_t baseX = cx * 16, baseZ = cz * 16;
    // Gather first, insert after: taking tiles_mtx_ per block would
    // serialise the whole scan against every other tile lookup.
    std::vector<std::shared_ptr<cursestone::tile::Tile>> fresh;
    for (int32_t y = 0; y < 128; ++y)
        for (int32_t lz = 0; lz < 16; ++lz)
            for (int32_t lx = 0; lx < 16; ++lx) {
                const uint8_t id = mcp_chunk_get_block_id(c, lx, y, lz);
                if (!isTileBlock(id)) continue;
                const int32_t wx = baseX + lx, wz = baseZ + lz;
                {
                    std::lock_guard<std::mutex> lk(tiles_mtx_);
                    if (tiles_.count(packTilePos(wx, y, wz))) continue;
                }
                fresh.push_back(createTileForBlock(
                    id, cursestone::world::Position((double)wx, (double)y,
                                                    (double)wz)));
            }
    if (fresh.empty()) return;

    std::lock_guard<std::mutex> lk(tiles_mtx_);
    for (auto& t : fresh)
        if (t)
            tiles_[packTilePos((int32_t)t->position().x, (int32_t)t->position().y,
                               (int32_t)t->position().z)] = std::move(t);
}

void World::syncTilesToChunks() {
    // Snapshot ALL live tiles (not just dirty ones) grouped by owning
    // chunk, released from the lock before touching chunks_ (which has
    // its own internal locking) to avoid to lock-order dependency between
    // tiles_mtx_ and ChunkManager's mutex.
    //
    // Why ALL tiles and not just dirty ones: to chunk's tile_entities
    // array is rebuilt from scratch below (mcp_chunk_clear_tile_entities
    // then re-add). If we only fed it the dirty tiles, to chunk with one
    // dirty chest and one untouched chest would lose the untouched
    // chest's blob entirely — clear_tile_entities doesn't know which
    // entries "should" survive, it just empties the array.
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<cursestone::tile::Tile>>> byChunk;
    bool anyDirty = false;
    std::vector<uint64_t> emptiedChunks;
    {
        std::lock_guard<std::mutex> lk(tiles_mtx_);
        for (auto& [_, t] : tiles_) {
            if (!t) continue;
            if (t->isDirty()) anyDirty = true;
            int32_t cx = static_cast<int32_t>(t->position().x) >> 4;
            int32_t cz = static_cast<int32_t>(t->position().z) >> 4;
            byChunk[packChunkPos(cx, cz)].push_back(t);
        }
        // chunks that lost to tile need rebuilding even when they hold not
        // live tiles left: seed them with an empty list so the loop below
        // clears their stale tile_entities array instead of skipping them.
        for (uint64_t ck : tiles_dirty_chunks_) {
            if (byChunk.find(ck) == byChunk.end()) byChunk[ck] = {};
        }
        anyDirty = anyDirty || !tiles_dirty_chunks_.empty();
    }
    if (!anyDirty) return;  // nothing changed since the last save()

    for (auto& [ckey, tilesInChunk] : byChunk) {
        // Only rebuild to chunk's tile_entities array if something in it
        // actually changed — skips the write+serialize cost for every
        // chunk holding only untouched tiles.
        bool chunkHasDirty = false;
        for (auto& t : tilesInChunk) if (t->isDirty()) { chunkHasDirty = true; break; }
        bool emptied = false;
        {
            std::lock_guard<std::mutex> lk(tiles_mtx_);
            emptied = tiles_dirty_chunks_.count(ckey) != 0;
        }
        if (!chunkHasDirty && !emptied) continue;

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
        if (emptied) {
            std::lock_guard<std::mutex> lk(tiles_mtx_);
            tiles_dirty_chunks_.erase(ckey);
        }
    }
}

// ============================================================
// Chunk memory eviction
// ============================================================
void World::evictTilesForChunk(int32_t cx, int32_t cz) {
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

int World::unloadDistantChunks(const std::vector<ChunkViewer>& viewers, int32_t margin) {
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
//   - The `data` field MUST carry to non-zero strength or the 0.15.10
//     client renders nothing (clear sky): rain rolls 90000..110000,
//     thunder 30000..40000.
//   - Infinity sends the pair as reset-then-set: STOP_RAIN +
//     STOP_THUNDER always first, then the START events for the new
//     state. Re-sending the full pair is idempotent on the client and
//     guarantees to clean transition between any two states.
void World::sendWeatherTo(Player* p) {
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

void World::setWeather(int state, int32_t durationTicks) {
    if (state < WEATHER_CLEAR || state > WEATHER_THUNDER) state = WEATHER_CLEAR;
    bool changed = (weather_state_ != state);
    weather_state_ = state;
    weather_ticks_left_ = durationTicks > 0 ? durationTicks : -1;
    // Re-roll the strengths on every change, like Infinity's setWeather
    // (mt_rand(90000,110000) / mt_rand(30000,40000)).
    rain_strength_    = 90000 + (uint32_t)(rand() % 20001);
    thunder_strength_ = 30000 + (uint32_t)(rand() % 10001);
    for (auto& p : Server::getInstance().playersSnapshot()) {
        if (p && p->spawned() && p->getWorld() == this) sendWeatherTo(p.get());
    }
    if (changed) {
        static const char* kNames[4] = { "clear", "rain", "rain+thunder", "thunder" };
        utils::MainLogger::get().info(std::string("[Weather] World '") + cfg_.name +
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
    LightningRemovalTask(int64_t eid, World* world) : eid_(eid), world_(world) {}
    void onRun() override {
        auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
        rm->eid = eid_;
        for (auto& p : Server::getInstance().playersSnapshot()) {
            if (p && p->spawned() && p->getWorld() == world_) p->sendPacket(rm);
        }
    }
private:
    int64_t eid_;
    World* world_;   // world where cayó the rayo
};

} // namespace

void World::spawnLightning(double x, double y, double z, bool withFire) {
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
        if (p && p->spawned() && p->getWorld() == this) {
            p->sendPacket(add);
            p->sendPacket(exp);
        }
    }

    // Despawn (Infinity: age > 20 ticks).
    scheduler::ServerScheduler::get().scheduleSync(
        std::make_shared<LightningRemovalTask>(eid, this), 20);

    // Damage + ignite players inside Infinity's grow(4, 3, 4) box:
    // mt_rand(8, 20) damage, setOnFire(mt_rand(3, 8)) — survival only.
    if (Server::getInstance().difficulty() > 0) {
        for (auto& p : Server::getInstance().playersSnapshot()) {
            if (!p || !p->spawned()) continue;
            if (p->getWorld() != this) continue;   // same world
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
            uint8_t below = by > 0 ? getBlockId(bx, by - 1, bz)
                                   : static_cast<uint8_t>(block::AIR);
            bool below_liquid = (below == block::WATER || below == block::FLOWING_WATER ||
                                 below == block::LAVA  || below == block::FLOWING_LAVA);
            if ((at == block::AIR || at == block::TALL_GRASS) && !below_liquid) {
                setBlock(bx, by, bz, block::FIRE, 0);
                auto up = std::make_shared<network::protocol::UpdateBlockPacket>();
                up->x = bx; up->z = bz;
                up->y = (uint8_t)by;
                up->blockId = block::FIRE;
                up->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
                for (auto& p : Server::getInstance().playersSnapshot()) {
                    if (p && p->spawned() && p->getWorld() == this) p->sendPacket(up);
                }
            }
        }
    }
}

void World::strikeRandomPlayerLightning() {
    // Infinity Weather::calcWeather: pick to random player, strike at
    // (p.x + mt_rand(-64, 64), highestBlock, p.z + mt_rand(-64, 64)).
    auto players = Server::getInstance().playersSnapshot();
    Player* target = nullptr;
    for (auto& p : players) {
        if (p && p->spawned() && p->getWorld() == this) { target = p.get(); break; }
    }
    if (!target) return;

    const auto& tpos = target->getNextPosition();
    int32_t bx = (int32_t)std::floor(tpos.x) + (std::rand() % 129 - 64);
    int32_t bz = (int32_t)std::floor(tpos.z) + (std::rand() % 129 - 64);
    if (!isChunkLoaded(bx, bz)) return;   // strike fell outside loaded world
    mcp_chunk_t* c = chunks_.getChunkIfLoaded(bx >> 4, bz >> 4);
    if (!c || !c->height_map) return;
    // height_map = topmost light-blocking block + 1 => first AIR and.
    int32_t y = c->height_map[((bz & 15) << 4) | (bx & 15)];
    if (y <= 0 || y > 255) return;
    spawnLightning((double)bx + 0.5, (double)y, (double)bz + 0.5, /*withFire=*/true);
}

// ---------------------------------------------------------------
// doWeatherTick — called from World::tick() every server tick.
//
//  1. Counts down rain/thunder durations (when bounded).
//  2. While raining, extinguishes exposed fire: walks to rotating
//     window of CHUNKS_PER_TICK loaded chunks per tick and replaces
//     every FIRE block whose column has sky above it with AIR.
//     Chunk scans read section->blocks directly (4096 bytes each,
//     not per-block accessor call); per-column sky checks use the
//     height map, so "exposed" means "sky directly above", matching
//     how the rain visibly falls. Vanilla PMMP also has to chance
//     component; we keep it deterministic here and cap the work per
//     tick (MAX_EXTINGUISH_PER_TICK) so to large burning area can
//     never stall the tick loop (the same philosophy that killed
//     fire propagation in v6).
//  3. Sends UpdateBlockPacket to every online player for each block
//     changed, so clients see the fire die instead of finding out on
//     the next chunk resync.
// ---------------------------------------------------------------
void World::doWeatherTick(uint64_t current_tick) {
    // 1. Duration countdown.
    if (weather_ticks_left_ > 0) {
        if (--weather_ticks_left_ <= 0) setWeather(WEATHER_CLEAR);
    }

    // 1.5. Lightning strikes (Infinity Weather::calcWeather strikes
    // when weatherNow >= RAINY_THUNDER, i.e. states 2 AND 3 — this is
    // also why the check sits above the isRaining() early-return:
    // state 3 thunders without rain). Every LIGHTNING_STRIKE_INTERVAL
    // ticks, strike within ±64 blocks of to random player. With not
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
            // every chunk when there is not fire anywhere.
            if (!std::memchr(sec->blocks, (int)block::FIRE, 4096)) continue;

            // bug FIX ("extinguished 6 fire block(s)" spam, fire never
            // dies): the scan previously wrapped the j-loop in an extra
            // `ly` 0..15 loop that re-walked the whole 4096-block array
            // 16 times, while deriving the world and from `ly` instead of
            // from the hit index. The blocks array is XZY-packed
            // (x | z<<4 | and<<8), so j>>8 IS the section-relative and — the
            // old code set AIR 16 blocks below the actual fire and counted
            // the same fire once per mismatched ly, so the memory never
            // changed and the log repeated forever. and now comes from j.
            for (int j = 0; j < 4096 && extinguished < MAX_EXTINGUISH_PER_TICK; ++j) {
                if (sec->blocks[j] != (uint8_t)block::FIRE) continue;
                int lx = j & 15;
                int lz = (j >> 4) & 15;
                int32_t y = ((int32_t)sy << 4) | (j >> 8);
                // Exposed = sky visible above this column. The height
                // map stores topmost light-blocking block + 1 per
                // column, and FIRE itself never inflates it
                // (mcp_block_light_opacity(FIRE) == 0), so to fire
                // whose top face touches open sky has and >= h here.
                int32_t h = c->height_map ? c->height_map[(lz << 4) | lx] : 256;
                if (y < h) continue;
                int32_t wx = (cx << 4) | lx;
                int32_t wz = (cz << 4) | lz;
                // Route through World::setBlock so the chunk dirty
                // flag is set (and stays on the same write path as
                // the rest of the server).
                setBlock(wx, y, wz, block::AIR, 0);
                auto up = std::make_shared<network::protocol::UpdateBlockPacket>();
                up->x = wx; up->z = wz;
                up->y = (uint8_t)y;
                up->blockId = block::AIR;
                up->blockData = 0;
                up->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
                for (auto& p : Server::getInstance().playersSnapshot()) {
                    if (p && p->spawned() && p->getWorld() == this) p->sendPacket(up);
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

} // namespace cursestone::world
