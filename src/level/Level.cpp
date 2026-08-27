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

#include <sys/stat.h>
#include <sys/types.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace atomikpe::level {

static void ensureDir(const std::string& path) {
    ::mkdir(path.c_str(), 0755);
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

void Level::setBlock(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t data) {
    int32_t cx = x >> 4, cz = z >> 4;
    if (mcp_chunk_t* c = chunks_.getChunk(cx, cz)) {
        mcp_chunk_set_block_full(c, x & 15, y, z & 15, id, data);
        c->dirty = true;
    }
}

int Level::save() {
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

} // namespace atomikpe::level
