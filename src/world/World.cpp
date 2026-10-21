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
// World implementation; connects the provider and chunk manager for disk persistence.
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
#include "item/ItemPotion.h"
#include "network/protocol/ExplodePacket.h"
#include <cmath>
#include "network/protocol/RemoveEntityPacket.h"
#include "network/protocol/MoveEntityPacket.h"
#include "network/protocol/SetEntityMotionPacket.h"
#include "network/protocol/SetEntityDataPacket.h"
#include "network/protocol/AddItemEntityPacket.h"
#include "network/protocol/EntityEventPacket.h"
#include "world/particle/Particle.h"
#include "world/particle/GenericParticle.h"
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

// Creates the directory and any missing parent directories.
static void ensureDir(const std::string& path) {
    if (path.empty())
        return;
    std::string partial;
    partial.reserve(path.size());
    size_t i = 0;
    // Skip to leading '/' so we don't try to mkdir("") first.
    if (path[0] == '/') {
        partial += '/';
        i = 1;
    }
    while (i <= path.size()) {
        if (i == path.size() || path[i] == '/') {
            if (!partial.empty() && partial != "/") {
                struct stat st{};
                if (::stat(partial.c_str(), &st) != 0) {
                    ::mkdir(partial.c_str(), 0755);
                }
            }
        }
        if (i < path.size() && path[i] == '/')
            partial += '/';
        else if (i < path.size())
            partial += path[i];
        ++i;
    }
}

World::World(WorldConfig cfg) : cfg_(std::move(cfg)), light_(*this) {
    if (cfg_.dimension == 0 && cfg_.generator == "nether") {
        cfg_.dimension = 1;
    }


    std::string worldsRoot = cfg_.dataPath.empty() ? "worlds" : (cfg_.dataPath + "/worlds");
    std::string worldDir = worldsRoot + "/" + cfg_.name;
    ensureDir(worldsRoot);
    ensureDir(worldDir);

    portals_path_ = worldDir + "/portals.dat";
    portals_.load(portals_path_);

    auto& mgr = format::WorldProviderManager::getInstance();
    auto provider_unique = mgr.create(cfg_.format);
    if (!provider_unique) {
        utils::MainLogger::get().warn("Unknown level format '" + cfg_.format +
                                      "'; falling back to default '" + mgr.defaultFormatName() +
                                      "'");
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
            provider->setSeed(cfg_.seed);
        }
    }

    if (cfg_.generator == "flat") {
        // Default flat preset: bedrock / 2x dirt / grass.
        std::vector<generator::FlatLayer> layers = {
            {7, 1}, // Bedrock.
            {3, 2}, // Dirt.
            {2, 1}, // Grass.
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
        provider->setSpawn(math::Vector3{gen_spawn.x, gen_spawn.y, gen_spawn.z});
        provider->setCurrentTime(cfg_.time);
        if (provider->getSpawn().y > 0) {
            spawn_ = mcp_v3(provider->getSpawn().x + 0.5, provider->getSpawn().y,
                            provider->getSpawn().z + 0.5);
        }
        utils::MainLogger::get().info("World '" + cfg_.name + "' loaded from " + worldDir +
                                      " (format=" + provider->getName() +
                                      ", seed=" + std::to_string(cfg_.seed) +
                                      ", generator=" + cfg_.generator + ")");
    } else {
        utils::MainLogger::get().error("Failed to create level provider for format '" +
                                       cfg_.format + "' — chunks will be in-memory only!");
    }

    chunks_.setProvider(provider);
    chunks_.setGenerator(generator_);
    provider_ = provider;
    chunks_.setOnChunkAdded([this](int32_t cx, int32_t cz) {
        onChunkAdded(cx, cz);
    });

    {
        int32_t sbx = (int32_t)std::floor(spawn_.x);
        int32_t sbz = (int32_t)std::floor(spawn_.z);
        if (chunks_.getChunk(sbx >> 4, sbz >> 4) != nullptr) {
            for (int y = 127; y > 0; --y) {
                uint8_t id = getBlockId(sbx, y, sbz);
                if (id == 0)
                    continue; // Air.
                if (id == block::LEAVES || id == block::LEAVES_2)
                    continue; // Canopy.
                if (id == block::BEDROCK)
                    continue;
                uint8_t above = getBlockId(sbx, y + 1, sbz);
                if (above == 0 || above == block::WATER || above == block::FLOWING_WATER) {
                    spawn_.y = (double)y + 1.0;
                    break;
                }
                // Solid with solid above (tree trunk / overhang): keep down.
            }
            if (provider_) {
                provider_->setSpawn(math::Vector3{(double)spawn_.x, spawn_.y, (double)spawn_.z});
            }
            utils::MainLogger::get().info(
                "Spawn snapped to terrain: " + std::to_string((int)spawn_.x) + ", " +
                std::to_string((int)spawn_.y) + ", " + std::to_string((int)spawn_.z));
        }
    }
}

World::~World() {
    if (provider_)
        provider_->shutdown();
}

namespace {
bool dropBlockSolid(uint8_t id);
bool fallableBlockId(uint8_t id);
} // namespace

uint8_t World::getBlockId(int32_t x, int32_t y, int32_t z) {
    int32_t cx = x >> 4, cz = z >> 4;
    mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
    if (!c)
        return 0;
    return mcp_chunk_get_block_id(c, x & 15, y, z & 15);
}

std::vector<physics::BlockBox> World::collisionBoxes(mcp_aabb_t region) {
    std::vector<physics::BlockBox> out;
    out.reserve(16);

    int x0 = (int)std::floor(region.minX), x1 = (int)std::ceil(region.maxX);
    int y0 = (int)std::floor(region.minY), y1 = (int)std::ceil(region.maxY);
    int z0 = (int)std::floor(region.minZ), z1 = (int)std::ceil(region.maxZ);

    for (int bx = x0; bx <= x1; ++bx) {
        for (int by = y0; by <= y1; ++by) {
            for (int bz = z0; bz <= z1; ++bz) {
                uint8_t id = getBlockId(bx, by, bz);
                if (!dropBlockSolid(id))
                    continue;

                mcp_aabb_t local = mcp_aabb_make(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
                block::Block* b = block::Block::get(id);
                if (b) {
                    mcp_aabb_t bb = b->getBoundingBox();
                    if (bb.maxX > bb.minX && bb.maxY > bb.minY && bb.maxZ > bb.minZ) {
                        local = bb;
                    }
                }

                physics::BlockBox box;
                box.x = bx;
                box.y = by;
                box.z = bz;
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
    if (!c)
        return 0;
    return mcp_chunk_get_block_data(c, x & 15, y, z & 15);
}

bool World::setFurnaceFlame(int32_t x, int32_t y, int32_t z, bool lit) {
    int32_t cx = x >> 4, cz = z >> 4;
    mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
    if (!c)
        return false;
    const uint8_t want = lit ? (uint8_t)block::BURNING_FURNACE : (uint8_t)block::FURNACE;
    const uint8_t have = mcp_chunk_get_block_id(c, x & 15, y, z & 15);
    if (have == want)
        return false;
    const uint8_t data = mcp_chunk_get_block_data(c, x & 15, y, z & 15);
    mcp_chunk_set_block_full(c, x & 15, y, z & 15, want, data);
    c->dirty = true;
    return true;
}

void World::setBlock(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t data) {
    int32_t cx = x >> 4, cz = z >> 4;
    if (mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz)) {
        mcp_chunk_set_block_full(c, x & 15, y, z & 15, id, data);
        mcp_chunk_recalculate_height_map_column(c, x & 15, z & 15);
        c->dirty = true;
        light_.relightAround(x, y, z, LightEngine::kRelightRadius);

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
    light_.onChunkAdded(cx, cz);
}

int World::save() {
    // Portal registry: tiny text file, rewritten on every full save.
    portals_.save(portals_path_);
    syncTilesToChunks();
    int saved = chunks_.flushDirty();
    if (provider_)
        provider_->flushPendingSaves();
    if (provider_) {
        provider_->setSpawn(math::Vector3{(double)spawn_.x, (double)spawn_.y, (double)spawn_.z});
        provider_->setCurrentTime(cfg_.time);
        provider_->saveLevelData();
    }
    return saved;
}

int World::saveBudget(size_t max_chunks, uint64_t max_ms, bool write_level_data) {
    syncTilesToChunks();
    int saved = chunks_.flushDirtyBudget(max_chunks, max_ms);
    if (write_level_data && provider_) {
        provider_->setSpawn(math::Vector3{(double)spawn_.x, (double)spawn_.y, (double)spawn_.z});
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
    if (sleep_ticks_ > 0 && --sleep_ticks_ <= 0) {
        checkSleep();
    }
    light_.processPending(LightEngine::kPendingBudget);
    tickDroppedItems(current_tick);
    tickArrows(current_tick);
    tickSplashPotions(current_tick);
    tickFallingBlocks(current_tick);
}

void World::checkSleep() {
    auto& srv = cursestone::Server::getInstance();
    auto players = srv.playersSnapshot();
    if (players.empty())
        return;

    bool resetTime = true;
    for (auto& p : players) {
        if (!p || p->isPendingRemoval())
            continue; // quitting players don't count
        if (p->getWorld() != this)
            continue; // other levels don't vote here
        if (!p->isSleeping()) {
            resetTime = false;
            break;
        }
    }

    if (!resetTime)
        return;

    int32_t time = cfg_.time % TIME_FULL;
    if (time >= TIME_NIGHT && time < TIME_SUNRISE) {
        // Jump to the next morning (next multiple of TIME_FULL).
        cfg_.time = cfg_.time + TIME_FULL - time;

        for (auto& p : players) {
            if (!p)
                continue;
            if (p->getWorld() != this)
                continue;
            if (p->isSleeping())
                p->stopSleep();
        }

        auto pk = std::make_shared<network::protocol::SetTimePacket>();
        pk->time = cfg_.time;
        pk->started = true;
        for (auto& p : players) {
            if (!p || p->getWorld() != this)
                continue;
            p->sendPacket(pk);
        }

        utils::MainLogger::get().info("[SLEEP] Everyone is asleep in \"" + cfg_.name +
                                      "\" — time skipped to morning (" +
                                      std::to_string(cfg_.time % TIME_FULL) + ")");
    }
}

namespace {

constexpr double kItemGravity = 0.04;  // item entity gravity
constexpr double kItemDrag = 0.02;     // item entity drag
constexpr double kBlockFriction = 0.6; // Block::getFrictionFactor()

// the block solidity table — see the forward declaration above.
bool dropBlockSolid(uint8_t id) {
    switch (id) {
    case block::AIR:
    case block::FLOWING_WATER:
    case block::WATER:
    case block::FLOWING_LAVA:
    case block::LAVA:
    case block::FIRE:
    case block::COBWEB:
    case block::SAPLING:
    case block::TALL_GRASS:
    case block::DEAD_BUSH:
    case block::FLOWER_YELLOW:
    case block::FLOWER_RED:
    case block::BROWN_MUSHROOM:
    case block::RED_MUSHROOM:
    case block::TORCH:
    case block::REDSTONE_TORCH_ON:
    case block::REDSTONE_WIRE:
    case block::LEVER:
    case block::RAIL:
    case block::POWERED_RAIL:
    case block::DETECTOR_RAIL:
    case block::WHITE_CARPET:
    case block::SNOW_LAYER:
    case block::LILY_PAD:
    case block::VINE:
    case block::SUGARCANE:
    case block::NETHER_PORTAL:
        return false;
    default:
        return true;
    }
}


bool fallableBlockId(uint8_t id) {
    switch (id) {
    case block::SAND:   // 12 — also red sand via meta 1
    case block::GRAVEL: // 13
    case block::ANVIL:  // 145 — direction bits 0x03, damage 0x0c
        return true;
    default:
        return false;
    }
}

bool liquidForFalling(uint8_t id) {
    switch (id) {
    case block::FLOWING_WATER:
    case block::WATER:
    case block::FLOWING_LAVA:
    case block::LAVA:
        return true;
    default:
        return false;
    }
}

void sendUpdateBlock(World* w, int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t data) {
    auto pk = std::make_shared<network::protocol::UpdateBlockPacket>();
    pk->x = x;
    pk->z = z;
    pk->y = (uint8_t)y;
    pk->blockId = id;
    pk->blockData = data;
    pk->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
    int32_t cx = x >> 4, cz = z >> 4;
    for (auto& p : cursestone::Server::getInstance().playersSnapshot()) {
        if (!p || !p->spawned())
            continue;
        if (p->getWorld() != w)
            continue; // Same world.
        if (!p->canSeeBlock(cx, cz))
            continue;
        p->sendPacket(pk);
    }
}

void sendToChunkViewers(World* w, int32_t cx, int32_t cz,
                        const std::shared_ptr<network::protocol::DataPacket>& pk) {
    for (auto& p : cursestone::Server::getInstance().playersSnapshot()) {
        if (!p || !p->spawned())
            continue;
        if (p->getWorld() != w)
            continue; // Same world.
        if (!p->canSeeBlock(cx, cz))
            continue;
        p->sendPacket(pk);
    }
}

void sendRemoveEntity(World* w, int64_t eid) {
    auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
    rm->eid = eid;
    for (auto& p : cursestone::Server::getInstance().playersSnapshot()) {
        if (!p || !p->spawned())
            continue;
        if (p->getWorld() != w)
            continue;
        p->sendPacket(rm);
    }
}

void anvilLandingEffects(World* w, int32_t bx, int32_t by, int32_t bz, float fall_distance) {
    auto snd = std::make_shared<network::protocol::LevelEventPacket>();
    snd->evid = 1022; // EVENT_SOUND_ANVIL_FALL (protocol 84)
    snd->x = (float)bx + 0.5f;
    snd->y = (float)by + 0.5f;
    snd->z = (float)bz + 0.5f;
    snd->data = 0;
    sendToChunkViewers(w, bx >> 4, bz >> 4, snd);

    float dmg = (fall_distance - 1.0f) * 2.0f;
    if (dmg > 40.0f)
        dmg = 40.0f;
    if (dmg <= 0.0f)
        return;

    double minx = (double)bx + 0.5 - 0.49 - 0.1, maxx = (double)bx + 0.5 + 0.49 + 0.1;
    double miny = (double)by - 0.1, maxy = (double)by + 0.98 + 0.1;
    double minz = (double)bz + 0.5 - 0.49 - 0.1, maxz = (double)bz + 0.5 + 0.49 + 0.1;
    for (auto& p : cursestone::Server::getInstance().playersSnapshot()) {
        if (!p || !p->spawned() || p->isDead() || p->isPendingRemoval())
            continue;
        if (p->getWorld() != w)
            continue;
        const math::Vector3& pp = p->getNextPosition();
        // Player AABB: [x±0.3, y .. y+1.8, z±0.3] (Human dimensions).
        if (pp.x + 0.3 <= minx || pp.x - 0.3 >= maxx)
            continue;
        if (pp.y + 1.8 <= miny || pp.y >= maxy)
            continue;
        if (pp.z + 0.3 <= minz || pp.z - 0.3 >= maxz)
            continue;
        p->damage(dmg, event::entity::DamageCause::FALL);
    }
}

} // namespace

void World::tickDroppedItems(uint64_t current_tick) {
    std::vector<DroppedItem*> snapshot;
    {
        std::lock_guard<std::mutex> lk(dropped_items_mtx_);
        snapshot.reserve(dropped_items_.size());
        for (auto& [_, di] : dropped_items_) {
            if (di && !di->taken)
                snapshot.push_back(di.get());
        }
    }
    if (snapshot.empty())
        return;

    (void)current_tick;

    for (DroppedItem* di : snapshot) {
        int item_cx = (int)std::floor(di->x) >> 4;
        int item_cz = (int)std::floor(di->z) >> 4;
        if (chunks_.getChunkIfLoaded(item_cx, item_cz) == nullptr)
            continue;
        if (di->y < 0.0 || di->y > 255.0)
            continue;

        // pickupDelay -= tickDiff (never above 32767 = infinite).
        if (di->pickup_delay_ticks > 0 && di->pickup_delay_ticks < 32767) {
            --di->pickup_delay_ticks;
        }

        di->vy -= kItemGravity;

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
                if (f0) {
                    limit = diffX;
                    direction = 0;
                }
                if (f1 && 1 - diffX < limit) {
                    limit = 1 - diffX;
                    direction = 1;
                }
                if (f2 && diffY < limit) {
                    limit = diffY;
                    direction = 2;
                }
                if (f3 && 1 - diffY < limit) {
                    limit = 1 - diffY;
                    direction = 3;
                }
                if (f4 && diffZ < limit) {
                    limit = diffZ;
                    direction = 4;
                }
                if (f5 && 1 - diffZ < limit) {
                    direction = 5;
                }

                double force = (double)std::rand() / (double)RAND_MAX * 0.2 + 0.1;
                switch (direction) {
                case 0:
                    di->vx = -force;
                    break;
                case 1:
                    di->vx = force;
                    break;
                case 2:
                    di->vy = -force;
                    break;
                case 3:
                    di->vy = force;
                    break;
                case 4:
                    di->vz = -force;
                    break;
                case 5:
                    di->vz = force;
                    break;
                default:
                    break;
                }
            }
        }

        if (di->on_ground) {
            constexpr double kAttractRadius = 2.0;
            constexpr double kAttractSpeed = 0.35;
            double best_dist = kAttractRadius;
            double bdx = 0.0, bdy = 0.0, bdz = 0.0;
            bool attracted = false;
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (!p || !p->spawned() || p->isDead())
                    continue;
                if (p->getWorld() != this)
                    continue; // Same world.
                math::Vector3 pp = p->getNextPosition();
                if (itemWithinPickupVolume(*di, pp.x, pp.y, pp.z))
                    continue;
                double adx = pp.x - di->x;
                double ady = (pp.y + 0.9) - di->y; // converge at the chest
                double adz = pp.z - di->z;
                double dist = std::sqrt(adx * adx + ady * ady + adz * adz);
                if (dist <= 0.0001 || dist >= best_dist)
                    continue;
                best_dist = dist;
                bdx = adx;
                bdy = ady;
                bdz = adz;
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
                    di->vx *= k;
                    di->vy *= k;
                    di->vz *= k;
                }
            }
        }

        const double movX = di->vx, movY = di->vy, movZ = di->vz;
        {
            mcp_aabb_t bb =
                mcp_aabb_make(di->x - kItemHalfW, di->y, di->z - kItemHalfW, di->x + kItemHalfW,
                              di->y + kItemHeight, di->z + kItemHalfW);

            mcp_aabb_t lookup = mcp_aabb_clone_move(bb, movX, movY, movZ);
            const std::vector<physics::BlockBox> boxes = collisionBoxes(lookup);

            physics::MotionResult res = physics::sweep(bb, mcp_v3(movX, movY, movZ), boxes);
            di->x += res.motion.x;
            di->y += res.motion.y;
            di->z += res.motion.z;

            di->on_ground = res.on_ground;
            if (res.clipped_x)
                di->vx = 0;
            if (res.clipped_y)
                di->vy = 0;
            if (res.clipped_z)
                di->vz = 0;
        }

        double friction = 1.0 - kItemDrag;
        if (di->on_ground && (std::fabs(di->vx) > 0.00001 || std::fabs(di->vz) > 0.00001)) {
            friction = kBlockFriction * friction;
        }
        di->vx *= friction;
        di->vy *= 1.0 - kItemDrag;
        di->vz *= friction;
        if (di->on_ground)
            di->vy *= -0.5;
    }

    for (DroppedItem* di : snapshot) {
        double dx = di->x - di->lx, dy = di->y - di->ly, dz = di->z - di->lz;
        if (std::fabs(dx) < 0.0005 && std::fabs(dy) < 0.0005 && std::fabs(dz) < 0.0005)
            continue;
        di->lx = di->x;
        di->ly = di->y;
        di->lz = di->z;

        auto pk = std::make_shared<network::protocol::MoveEntityPacket>();
        pk->eid = di->eid;
        pk->x = (float)di->x;
        pk->y = (float)di->y;
        pk->z = (float)di->z;
        int32_t pk_cx = (int32_t)std::floor(di->x) >> 4;
        int32_t pk_cz = (int32_t)std::floor(di->z) >> 4;
        for (auto& p : Server::getInstance().playersSnapshot()) {
            if (!p || !p->spawned())
                continue;
            if (p->getWorld() != this)
                continue; // Same world.
            if (!p->canSeeBlock(pk_cx, pk_cz))
                continue;
            p->sendPacket(pk);
        }
    }
}

size_t World::collectDroppedItems(uint64_t current_tick) {
    constexpr uint64_t DESPAWN_TICKS = 6000; // 5 min at 20 TPS — vanilla EntityItem lifetime

    std::vector<int64_t> expired;
    {
        std::lock_guard<std::mutex> lk(dropped_items_mtx_);
        for (auto it = dropped_items_.begin(); it != dropped_items_.end();) {
            if (!it->second) {
                it = dropped_items_.erase(it);
                continue;
            }
            if (!it->second->taken && current_tick - it->second->spawn_tick >= DESPAWN_TICKS) {
                expired.push_back(it->first);
                it = dropped_items_.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (!expired.empty()) {
        auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
        // One packet per eid — RemoveEntityPacket carries one entity ID.
        for (size_t i = 0; i < expired.size(); ++i) {
            if (i > 0)
                rm = std::make_shared<network::protocol::RemoveEntityPacket>();
            rm->eid = expired[i];
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (p && p->spawned() && p->getWorld() == this)
                    p->sendPacket(rm);
            }
        }
    }

    size_t removed = expired.size();
    {
        std::lock_guard<std::mutex> lk(dropped_items_mtx_);
        for (auto it = dropped_items_.begin(); it != dropped_items_.end();) {
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

void World::addArrow(ArrowEntity a) {
    std::lock_guard<std::mutex> lk(arrows_mtx_);
    arrows_[a.eid] = std::make_unique<ArrowEntity>(std::move(a));
}

std::vector<ArrowEntity*> World::arrows() {
    std::vector<ArrowEntity*> out;
    std::lock_guard<std::mutex> lk(arrows_mtx_);
    out.reserve(arrows_.size());
    for (auto& [_, a] : arrows_) {
        if (a && !a->taken)
            out.push_back(a.get());
    }
    return out;
}

bool World::tryClaimArrow(int64_t eid) {
    std::lock_guard<std::mutex> lk(arrows_mtx_);
    auto it = arrows_.find(eid);
    if (it == arrows_.end() || !it->second)
        return false;
    if (it->second->taken)
        return false;
    it->second->taken = true;
    return true;
}

void World::addParticle(const particle::Particle& p) {
    const auto& pos = p.position();
    int32_t cx = (int32_t)std::floor(pos.x) >> 4;
    int32_t cz = (int32_t)std::floor(pos.z) >> 4;
    auto pk = std::make_shared<network::protocol::LevelEventPacket>();
    pk->evid = (int16_t)(network::protocol::LevelEventPacket::EVENT_ADD_PARTICLE_MASK |
                         (p.networkId() & 0xFFF));
    pk->x = (float)pos.x;
    pk->y = (float)pos.y;
    pk->z = (float)pos.z;
    pk->data = p.networkData();
    for (auto& pl : Server::getInstance().playersSnapshot()) {
        if (!pl || !pl->spawned() || pl->getWorld() != this)
            continue;
        if (!pl->canSeeBlock(cx, cz))
            continue;
        pl->sendPacket(pk);
    }
}

void World::tickArrows(uint64_t current_tick) {
    std::vector<ArrowEntity*> snapshot;
    {
        std::lock_guard<std::mutex> lk(arrows_mtx_);
        snapshot.reserve(arrows_.size());
        for (auto& [_, a] : arrows_) {
            if (a && !a->taken)
                snapshot.push_back(a.get());
        }
    }
    if (snapshot.empty())
        return;

    for (ArrowEntity* a : snapshot) {
        if (current_tick - a->spawn_tick > 1200) {
            a->taken = true;
            auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
            rm->eid = a->eid;
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (p && p->spawned() && p->getWorld() == this)
                    p->sendPacket(rm);
            }
            continue;
        }

        if (a->stuck) {
            ++a->flight_ticks;
            continue;
        }

        int acx = (int)std::floor(a->x) >> 4;
        int acz = (int)std::floor(a->z) >> 4;
        if (chunks_.getChunkIfLoaded(acx, acz) == nullptr)
            continue;
        ++a->flight_ticks;

        // motionY -= gravity (Arrow: 0.05)
        a->vy -= 0.05;

        double nx = a->x + a->vx;
        double ny = a->y + a->vy;
        double nz = a->z + a->vz;

        Player* hit = nullptr;
        double bestT = 2.0; // t in [0,1] along the segment
        for (auto& p : Server::getInstance().playersSnapshot()) {
            if (!p || !p->spawned() || p->isDead())
                continue;
            if (p->isPendingRemoval())
                continue;
            if (p->getWorld() != this)
                continue;
            // The shooter is immune to their own arrow: the projectile
            // spawns inside the shooter's own hitbox, so a freshly released
            // (slow or steep) shot would self-hit and show every other
            // player a phantom hurt animation on the shooter.
            if (p->runtimeId() == a->shooter_eid)
                continue;
            const auto& pp = p->getNextPosition();
            // Player AABB [x-0.3, y, z-0.3]..[x+0.3, y+1.8, z+0.3], grown 0.3.
            double minx = pp.x - 0.6, maxx = pp.x + 0.6;
            double miny = pp.y - 0.3, maxy = pp.y + 2.1;
            double minz = pp.z - 0.6, maxz = pp.z + 0.6;
            if (nx < minx && a->x < minx)
                continue;
            if (nx > maxx && a->x > maxx)
                continue;
            if (ny < miny && a->y < miny)
                continue;
            if (ny > maxy && a->y > maxy)
                continue;
            if (nz < minz && a->z < minz)
                continue;
            if (nz > maxz && a->z > maxz)
                continue;
            // Slab method: t interval where the segment is inside the box.
            double tmin = 0.0, tmax = 1.0;
            bool miss = false;
            const double so[3] = {a->x, a->y, a->z};
            const double sd[3] = {nx - a->x, ny - a->y, nz - a->z};
            const double blo[3] = {minx, miny, minz};
            const double bhi[3] = {maxx, maxy, maxz};
            for (int ax = 0; ax < 3; ++ax) {
                if (std::fabs(sd[ax]) < 1e-12) {
                    if (so[ax] < blo[ax] || so[ax] > bhi[ax]) {
                        miss = true;
                        break;
                    }
                    continue;
                }
                double t1 = (blo[ax] - so[ax]) / sd[ax];
                double t2 = (bhi[ax] - so[ax]) / sd[ax];
                if (t1 > t2)
                    std::swap(t1, t2);
                if (t1 > tmin)
                    tmin = t1;
                if (t2 < tmax)
                    tmax = t2;
                if (tmin > tmax) {
                    miss = true;
                    break;
                }
            }
            if (miss || tmin > bestT)
                continue;
            bestT = tmin;
            hit = p.get();
        }
        if (hit) {
            double speed = std::sqrt(a->vx * a->vx + a->vy * a->vy + a->vz * a->vz);
            float dmg = std::ceil(speed * a->damage);
            if (a->critical) {
                // damage += rand(0, (int)(damage / 2) + 1);
                dmg += (float)(std::rand() % ((int)(dmg / 2) + 2));
            }
            Player* shooter = nullptr;
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (p && p->runtimeId() == a->shooter_eid) {
                    shooter = p.get();
                    break;
                }
            }
            auto applied =
                shooter ? hit->damageByEntity(dmg, shooter, event::entity::DamageCause::PROJECTILE)
                        : hit->damage(dmg, event::entity::DamageCause::PROJECTILE);

            if (applied != cursestone::entity::DamageApply::CANCELLED) {
                if (applied == cursestone::entity::DamageApply::HURT) {
                    auto hurtEv = std::make_shared<network::protocol::EntityEventPacket>();
                    hurtEv->eid = hit->runtimeId();
                    hurtEv->event = network::protocol::EntityEventPacket::HURT_ANIMATION;
                    for (auto& other : Server::getInstance().playersSnapshot()) {
                        if (!other)
                            continue;
                        if (!other->spawned())
                            continue;
                        other->sendPacket(hurtEv);
                    }
                    auto sh = std::make_shared<network::protocol::SetHealthPacket>();
                    sh->health = std::max(0, (int32_t)hit->health());
                    hit->sendPacket(sh);
                }
            }

            a->taken = true;
            auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
            rm->eid = a->eid;
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (p && p->spawned() && p->getWorld() == this)
                    p->sendPacket(rm);
            }
            continue;
        }

        double maxStep = std::max(std::fabs(a->vx), std::max(std::fabs(a->vy), std::fabs(a->vz)));
        int steps = (int)std::ceil(maxStep / 0.5);
        if (steps < 1)
            steps = 1;
        if (steps > 8)
            steps = 8;
        bool collided = false;
        int hitStep = 0;
        for (int i = 1; i <= steps; ++i) {
            double sx = a->x + a->vx * ((double)i / steps);
            double sy = a->y + a->vy * ((double)i / steps);
            double sz = a->z + a->vz * ((double)i / steps);
            if (dropBlockSolid(
                    getBlockId((int)std::floor(sx), (int)std::floor(sy), (int)std::floor(sz)))) {
                collided = true;
                hitStep = i;
                break;
            }
        }
        if (collided) {
            double tFree = (double)(hitStep - 1) / steps;
            double tHit = (double)hitStep / steps;
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
            a->yaw = (float)(std::atan2(a->vx, a->vz) * 180.0 / 3.14159265358979323846);
            a->pitch = (float)(std::atan2(a->vy, fh0) * 180.0 / 3.14159265358979323846);
            a->x += a->vx * tFree;
            a->y += a->vy * tFree;
            a->z += a->vz * tFree;
            a->stuck = true;
            a->vx = a->vy = a->vz = 0.0;
        } else {
            a->x = nx;
            a->y = ny;
            a->z = nz;
        }

        // Void / build-limit despawn.
        if (a->y < 0.0 || a->y > 260.0) {
            a->taken = true;
            auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
            rm->eid = a->eid;
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (p && p->spawned() && p->getWorld() == this)
                    p->sendPacket(rm);
            }
            continue;
        }

        // ---- Yaw/pitch from motion ----
        if (!a->stuck) {
            double fh = std::sqrt(a->vx * a->vx + a->vz * a->vz);
            a->yaw = (float)(std::atan2(a->vx, a->vz) * 180.0 / 3.14159265358979323846);
            a->pitch = (float)(std::atan2(a->vy, fh) * 180.0 / 3.14159265358979323846);
        }

        if (a->critical && !a->stuck) {
            particle::GenericParticle cp(
                particle::PARTICLE_CRITICAL,
                Position(a->x + 0.25 + ((std::rand() % 201) - 100) / 500.0,
                         a->y + 0.25 + ((std::rand() % 201) - 100) / 500.0,
                         a->z + 0.25 + ((std::rand() % 201) - 100) / 500.0),
                2);
            addParticle(cp);
        }

        // ---- updateMovement():  Entity::updateMovement() thresholds ----
        auto sq = [](double v) {
            return v * v;
        };
        double dpos = sq(a->x - a->lx) + sq(a->y - a->ly) + sq(a->z - a->lz);
        double drot = sq((double)a->yaw - a->lyaw) + sq((double)a->pitch - a->lpitch);
        double dmot = sq(a->vx - a->lmx) + sq(a->vy - a->lmy) + sq(a->vz - a->lmz);
        double vlen2 = a->vx * a->vx + a->vy * a->vy + a->vz * a->vz;

        bool sendMove =
            collided || dpos > 0.04 || (drot > 2.25 && dmot > 0.0001 && vlen2 <= 0.00001);
        bool sendMotion = collided || dmot > 0.0025 || (dmot > 0.0001 && vlen2 <= 0.0001);
        if (!sendMove && !sendMotion)
            continue;

        int32_t vcx = (int32_t)std::floor(a->x) >> 4;
        int32_t vcz = (int32_t)std::floor(a->z) >> 4;

        if (sendMove) {
            a->lx = a->x;
            a->ly = a->y;
            a->lz = a->z;
            a->lyaw = a->yaw;
            a->lpitch = a->pitch;

            auto mv = std::make_shared<network::protocol::MoveEntityPacket>();
            mv->eid = a->eid;
            mv->x = (float)a->x;
            mv->y = (float)a->y;
            mv->z = (float)a->z;
            mv->yaw = a->yaw;
            mv->headYaw = a->yaw;
            mv->pitch = a->pitch;
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (!p || !p->spawned() || p->getWorld() != this)
                    continue;
                if (!p->canSeeBlock(vcx, vcz))
                    continue;
                p->sendPacket(mv);
            }
        }

        if (sendMotion) {
            a->lmx = a->vx;
            a->lmy = a->vy;
            a->lmz = a->vz;

            auto mo = std::make_shared<network::protocol::SetEntityMotionPacket>();
            mo->entities.push_back({a->eid, (float)a->vx, (float)a->vy, (float)a->vz});
            for (auto& p : Server::getInstance().playersSnapshot()) {
                if (!p || !p->spawned() || p->getWorld() != this)
                    continue;
                if (!p->canSeeBlock(vcx, vcz))
                    continue;
                p->sendPacket(mo);
            }
        }
    }

    {
        std::lock_guard<std::mutex> lk(arrows_mtx_);
        for (auto it = arrows_.begin(); it != arrows_.end();) {
            if (it->second && it->second->taken)
                it = arrows_.erase(it);
            else
                ++it;
        }
    }
}

void World::addSplashPotion(SplashPotionEntity p) {
    std::lock_guard<std::mutex> lk(splash_potions_mtx_);
    splash_potions_[p.eid] = std::make_unique<SplashPotionEntity>(p);
}

void World::tickSplashPotions(uint64_t current_tick) {
    std::vector<SplashPotionEntity*> snapshot;
    {
        std::lock_guard<std::mutex> lk(splash_potions_mtx_);
        for (auto& [_, p] : splash_potions_) {
            if (p && !p->taken)
                snapshot.push_back(p.get());
        }
    }
    if (snapshot.empty())
        return;

    for (SplashPotionEntity* p : snapshot) {
        //  > 1200 breaks the bottle wherever it is.
        if (current_tick - p->spawn_tick > 1200) {
            p->taken = true;
            continue;
        }
        int pcx = (int)std::floor(p->x) >> 4;
        int pcz = (int)std::floor(p->z) >> 4;
        if (chunks_.getChunkIfLoaded(pcx, pcz) == nullptr)
            continue;

        // ThrownPotion physics: gravity 0.1, drag 0.05 per tick.
        p->vy -= 0.1;
        p->vx *= 0.95;
        p->vy *= 0.95;
        p->vz *= 0.95;

        double nx = p->x + p->vx;
        double ny = p->y + p->vy;
        double nz = p->z + p->vz;

        bool broke = false;
        for (auto& pl : Server::getInstance().playersSnapshot()) {
            if (!pl || !pl->spawned() || pl->isDead() || pl->isPendingRemoval())
                continue;
            if (pl->getWorld() != this)
                continue;
            // The thrower cannot break its own bottle for 5 ticks.
            if (pl->runtimeId() == p->shooter_eid && current_tick - p->spawn_tick < 5)
                continue;
            const auto& pp = pl->getNextPosition();
            if (nx >= pp.x - 0.6 && nx <= pp.x + 0.6 && ny >= pp.y - 0.3 && ny <= pp.y + 2.1 &&
                nz >= pp.z - 0.6 && nz <= pp.z + 0.6) {
                broke = true;
                break;
            }
        }

        if (!broke) {
            double maxStep = std::max(std::fabs(p->vx), std::max(std::fabs(p->vy), std::fabs(p->vz)));
            int steps = (int)std::ceil(maxStep / 0.5);
            if (steps < 1)
                steps = 1;
            if (steps > 8)
                steps = 8;
            for (int i = 1; i <= steps; ++i) {
                double sx = p->x + p->vx * ((double)i / steps);
                double sy = p->y + p->vy * ((double)i / steps);
                double sz = p->z + p->vz * ((double)i / steps);
                if (dropBlockSolid(getBlockId((int)std::floor(sx), (int)std::floor(sy),
                                              (int)std::floor(sz)))) {
                    broke = true;
                    break;
                }
            }
        }
        // Off-world bottles break too (void / build limit).
        if (ny < 0.0 || ny > 260.0)
            broke = true;

        if (!broke) {
            p->x = nx;
            p->y = ny;
            p->z = nz;
            continue;
        }
        p->taken = true;

        // ---- Splash: particles, area effects, despawn ----
        uint8_t effect_id = 0;
        int duration = 0;
        int amplifier = 0;
        bool has_effect = item::ItemPotion::effectForMeta(p->meta, effect_id, duration, amplifier);
        uint32_t color = entity::Effect::colorOf(
            has_effect ? effect_id : (uint8_t)entity::EFF_WATER_BREATHING);
        // Break event first: the client plays the glass smash and draws the
        // splash swirl from this one packet (data = opaque ARGB color).
        {
            auto shatter = std::make_shared<network::protocol::LevelEventPacket>();
            shatter->evid = network::protocol::LevelEventPacket::EVENT_PARTICLE_SPLASH;
            shatter->x = (float)p->x;
            shatter->y = (float)p->y;
            shatter->z = (float)p->z;
            shatter->data = (int32_t)(0xFF000000u | (color & 0x00FFFFFFu));
            int32_t scx = (int32_t)std::floor(p->x) >> 4;
            int32_t scz = (int32_t)std::floor(p->z) >> 4;
            for (auto& pl : Server::getInstance().playersSnapshot()) {
                if (!pl || !pl->spawned() || pl->isPendingRemoval() || pl->getWorld() != this)
                    continue;
                if (!pl->canSeeBlock(scx, scz))
                    continue;
                pl->sendPacket(shatter);
            }
        }
        for (int i = 0; i < 8; ++i) {
            double ox = ((std::rand() % 21) - 10) / 20.0;
            double oy = ((std::rand() % 21) - 10) / 20.0;
            double oz = ((std::rand() % 21) - 10) / 20.0;
            addParticle(world::particle::GenericParticle(
                world::particle::PARTICLE_MOB_SPELL,
                world::Position{p->x + ox, p->y + oy, p->z + oz}, (int32_t)color));
        }
        if (has_effect) {
            for (auto& pl : Server::getInstance().playersSnapshot()) {
                if (!pl || !pl->spawned() || pl->isDead() || pl->isPendingRemoval())
                    continue;
                if (pl->getWorld() != this)
                    continue;
                const auto& pp = pl->getNextPosition();
                double dx = pp.x - p->x;
                double dy = (pp.y + 0.9) - p->y; // torso distance
                double dz = pp.z - p->z;
                if (dx * dx + dy * dy + dz * dz > 6.0 * 6.0)
                    continue;
                pl->addEffect(std::make_shared<entity::Effect>(
                    entity::Effect::create(effect_id, duration, amplifier)));
            }
        }
        auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
        rm->eid = p->eid;
        for (auto& pl : Server::getInstance().playersSnapshot()) {
            if (pl && pl->spawned() && pl->getWorld() == this)
                pl->sendPacket(rm);
        }
    }

    // Drop consumed records (same shape as the arrows sweep).
    {
        std::lock_guard<std::mutex> lk(splash_potions_mtx_);
        for (auto it = splash_potions_.begin(); it != splash_potions_.end();) {
            if (it->second && it->second->taken)
                it = splash_potions_.erase(it);
            else
                ++it;
        }
    }
}

namespace {

constexpr uint32_t kFallingBlockMaxAge = 600;

} // namespace

bool World::isFallableBlockId(uint8_t id) {
    return fallableBlockId(id);
}

void World::checkFallingSupport(int32_t x, int32_t y, int32_t z) {
    if (y < 0 || y >= 256)
        return;
    if (chunks_.getChunkIfLoaded(x >> 4, z >> 4) == nullptr)
        return;
    uint8_t id = getBlockId(x, y, z);
    if (!fallableBlockId(id))
        return;
    uint8_t below = (y > 0) ? getBlockId(x, y - 1, z) : (uint8_t)block::AIR;
    if (below != block::AIR && !liquidForFalling(below))
        return;
    spawnFallingBlock(x, y, z);
}

void World::spawnFallingBlock(int32_t bx, int32_t by, int32_t bz) {
    if (by < 0 || by >= 256)
        return;
    if (chunks_.getChunkIfLoaded(bx >> 4, bz >> 4) == nullptr)
        return;
    uint8_t cellId = getBlockId(bx, by, bz);
    if (!fallableBlockId(cellId))
        return;
    uint8_t cellData = getBlockData(bx, by, bz);

    int64_t eid = cursestone::Server::getInstance().allocateEid();

    setBlock(bx, by, bz, 0, 0);
    sendUpdateBlock(this, bx, by, bz, 0, 0);

    FallingBlockEntity fb;
    fb.eid = eid;
    fb.blockId = cellId;
    fb.blockData = cellData;
    fb.x = (double)bx + 0.5;
    fb.y = (double)by;
    fb.z = (double)bz + 0.5;
    fb.vx = fb.vy = fb.vz = 0.0;
    fb.lx = fb.x;
    fb.ly = fb.y;
    fb.lz = fb.z;
    fb.spawn_tick = cursestone::Server::getInstance().getCurrentTick();
    {
        std::lock_guard<std::mutex> lk(falling_blocks_mtx_);
        falling_blocks_[eid] = std::make_unique<FallingBlockEntity>(std::move(fb));
    }

    auto pk = std::make_shared<network::protocol::AddEntityPacket>();
    pk->eid = eid;
    pk->type = network::protocol::AddEntityPacket::TYPE_FALLING_BLOCK;
    pk->x = (float)(bx + 0.5);
    pk->y = (float)by;
    pk->z = (float)(bz + 0.5);
    pk->speedX = pk->speedY = pk->speedZ = 0.0f;
    pk->yaw = 0.0f;
    pk->pitch = 0.0f;
    network::protocol::SetEntityDataPacket::Entry meta;
    meta.index = network::protocol::SetEntityDataPacket::PROP_BLOCK_INFO;
    meta.type = network::protocol::SetEntityDataPacket::TYPE_INT;
    meta.ival = (int32_t)((uint32_t)cellId | ((uint32_t)cellData << 8));
    pk->metadata.push_back(meta);
    sendToChunkViewers(this, bx >> 4, bz >> 4, pk);

}

void World::dropItemAt(double x, double y, double z, uint16_t itemId, uint8_t count,
                       uint16_t damage) {
    if (itemId == 0 || count == 0)
        return;
    int64_t eid = cursestone::Server::getInstance().allocateEid();

    auto lcg = []() {
        return (double)std::rand() / (double)RAND_MAX;
    };

    auto pk = std::make_shared<network::protocol::AddItemEntityPacket>();
    pk->eid = eid;
    pk->itemId = (int16_t)itemId;
    pk->itemCount = count;
    pk->itemDamage = (int16_t)damage;
    pk->x = (float)x;
    pk->y = (float)y;
    pk->z = (float)z;
    pk->speedX = (float)(lcg() * 0.2 - 0.1);
    pk->speedY = 0.2f;
    pk->speedZ = (float)(lcg() * 0.2 - 0.1);
    sendToChunkViewers(this, (int32_t)std::floor(x) >> 4, (int32_t)std::floor(z) >> 4, pk);

    DroppedItem di;
    di.eid = eid;
    di.itemId = itemId;
    di.count = count;
    di.damage = damage;
    di.x = x;
    di.y = y;
    di.z = z;
    di.spawn_tick = cursestone::Server::getInstance().getCurrentTick();
    di.pickup_delay_ticks = 10; // World::dropItem() default (0.5 s)
    di.taken = false;
    di.vx = pk->speedX;
    di.vy = pk->speedY;
    di.vz = pk->speedZ;
    di.lx = x;
    di.ly = y;
    di.lz = z;
    {
        std::lock_guard<std::mutex> lk(dropped_items_mtx_);
        dropped_items_[eid] = std::make_unique<DroppedItem>(std::move(di));
    }
}

void World::tickFallingBlocks(uint64_t current_tick) {
    (void)current_tick; // age is per-record; no global tick dependency

    std::vector<FallingBlockEntity*> snapshot;
    {
        std::lock_guard<std::mutex> lk(falling_blocks_mtx_);
        snapshot.reserve(falling_blocks_.size());
        for (auto& [_, fb] : falling_blocks_) {
            if (fb && !fb->taken)
                snapshot.push_back(fb.get());
        }
    }
    if (snapshot.empty())
        return;
    std::sort(snapshot.begin(), snapshot.end(),
              [](const FallingBlockEntity* a, const FallingBlockEntity* b) {
                  if (a->y != b->y)
                      return a->y < b->y;
                  return a->eid < b->eid; // stable tie-break
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

        if (fb->y < 0.0 || fb->y > 256.0) {
            fb->taken = true;
            sendRemoveEntity(this, fb->eid);
            continue;
        }

        int fcx = (int)std::floor(fb->x) >> 4;
        int fcz = (int)std::floor(fb->z) >> 4;
        if (chunks_.getChunkIfLoaded(fcx, fcz) == nullptr)
            continue;

        // --- Gravity, then the swept move ( order) -----------
        fb->vy -= 0.04;

        mcp_aabb_t bb = mcp_aabb_make(fb->x - 0.49, fb->y, fb->z - 0.49, fb->x + 0.49, fb->y + 0.98,
                                      fb->z + 0.49);
        mcp_aabb_t lookup = mcp_aabb_clone_move(bb, fb->vx, fb->vy, fb->vz);
        const std::vector<physics::BlockBox> boxes = collisionBoxes(lookup);

        physics::MotionResult res = physics::sweep(bb, mcp_v3(fb->vx, fb->vy, fb->vz), boxes);
        fb->x += res.motion.x;
        fb->y += res.motion.y;
        fb->z += res.motion.z;

        if (res.motion.y < 0.0)
            fb->fall_distance += (float)(-res.motion.y);

        if (res.clipped_x)
            fb->vx = 0.0;
        if (res.clipped_z)
            fb->vz = 0.0;

        // --- Landing (: `if (onGround)`) ---------------
        if (res.on_ground) {
            fb->taken = true;

            int cx = (int)std::floor(fb->x);       // round(x - 0.5), x = cell + 0.5
            int cy = (int)std::floor(fb->y + 0.5); // round(y)
            int cz = (int)std::floor(fb->z);       // round(z - 0.5), z = cell + 0.5

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
            bool chunkOk =
                (cy >= 0 && cy < 256) && (chunks_.getChunkIfLoaded(cx >> 4, cz >> 4) != nullptr);
            if (chunkOk) {
                uint8_t cellId = getBlockId(cx, cy, cz);
                if (cellId == block::SNOW_LAYER) {
                    setBlock(cx, cy, cz, fb->blockId, fb->blockData);
                    sendUpdateBlock(this, cx, cy, cz, fb->blockId, fb->blockData);
                    if (fb->blockId == block::ANVIL)
                        anvilLandingEffects(this, cx, cy, cz, fb->fall_distance);
                } else if (cellId > 0 && !dropBlockSolid(cellId) && !liquidForFalling(cellId)) {
                    dropItemAt(fb->x, fb->y, fb->z, (uint16_t)fb->blockId, 1,
                               (uint16_t)fb->blockData);
                    if (fb->blockId == block::ANVIL)
                        anvilLandingEffects(this, cx, cy, cz, fb->fall_distance);
                } else {
                    setBlock(cx, cy, cz, fb->blockId, fb->blockData);
                    sendUpdateBlock(this, cx, cy, cz, fb->blockId, fb->blockData);
                    if (fb->blockId == block::ANVIL)
                        anvilLandingEffects(this, cx, cy, cz, fb->fall_distance);
                }
            }
            continue;
        }

        // --- drag (: friction = 1 - drag = 0.98 on all axes) --
        fb->vx *= 0.98;
        fb->vy *= 0.98;
        fb->vz *= 0.98;

        double dx = fb->x - fb->lx, dy = fb->y - fb->ly, dz = fb->z - fb->lz;
        if (std::fabs(dx) < 0.0005 && std::fabs(dy) < 0.0005 && std::fabs(dz) < 0.0005)
            continue;
        fb->lx = fb->x;
        fb->ly = fb->y;
        fb->lz = fb->z;

        auto mv = std::make_shared<network::protocol::MoveEntityPacket>();
        mv->eid = fb->eid;
        mv->x = (float)fb->x;
        mv->y = (float)fb->y;
        mv->z = (float)fb->z;
        int32_t vcx = (int32_t)std::floor(fb->x) >> 4;
        int32_t vcz = (int32_t)std::floor(fb->z) >> 4;
        sendToChunkViewers(this, vcx, vcz, mv);
    }

    {
        std::lock_guard<std::mutex> lk(falling_blocks_mtx_);
        for (auto it = falling_blocks_.begin(); it != falling_blocks_.end();) {
            if (it->second && it->second->taken)
                it = falling_blocks_.erase(it);
            else
                ++it;
        }
    }
}

size_t World::tickTiles(uint64_t current_tick) {
    std::vector<std::shared_ptr<cursestone::tile::Tile>> snapshot;
    {
        std::lock_guard<std::mutex> lk(tiles_mtx_);
        snapshot.reserve(tiles_.size());
        for (auto& [_, t] : tiles_)
            snapshot.push_back(t);
    }
    size_t n = 0;
    for (auto& t : snapshot) {
        if (!t)
            continue;
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

DroppedItem* World::droppedItem(int64_t eid) {
    std::lock_guard<std::mutex> lk(dropped_items_mtx_);
    auto it = dropped_items_.find(eid);
    if (it == dropped_items_.end())
        return nullptr;
    return it->second.get();
}

std::vector<DroppedItem*> World::droppedItems() {
    std::lock_guard<std::mutex> lk(dropped_items_mtx_);
    std::vector<DroppedItem*> out;
    out.reserve(dropped_items_.size());
    for (auto& [_, di] : dropped_items_) {
        if (di && !di->taken)
            out.push_back(di.get());
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
    if (it == dropped_items_.end())
        return false;
    DroppedItem* di = it->second.get();
    if (!di || di->taken)
        return false;
    di->taken = true;
    return true;
}

std::shared_ptr<cursestone::tile::Tile> World::getTile(int32_t x, int32_t y, int32_t z) {
    ensureTilesLoaded(x >> 4, z >> 4);
    std::lock_guard<std::mutex> lk(tiles_mtx_);
    auto it = tiles_.find(packTilePos(x, y, z));
    if (it == tiles_.end())
        return nullptr;
    return it->second;
}

void World::setTile(int32_t x, int32_t y, int32_t z, std::shared_ptr<cursestone::tile::Tile> t) {
    std::lock_guard<std::mutex> lk(tiles_mtx_);
    tiles_[packTilePos(x, y, z)] = std::move(t);
}

void World::removeTile(int32_t x, int32_t y, int32_t z) {
    std::lock_guard<std::mutex> lk(tiles_mtx_);
    auto it = tiles_.find(packTilePos(x, y, z));
    if (it == tiles_.end())
        return;
    tiles_.erase(it);
    tiles_dirty_chunks_.insert(packChunkPos(x >> 4, z >> 4));
}

std::vector<std::shared_ptr<cursestone::tile::Tile>> World::getTilesInChunk(int32_t cx,
                                                                            int32_t cz) {
    ensureTilesLoaded(cx, cz);
    std::vector<std::shared_ptr<cursestone::tile::Tile>> out;
    std::lock_guard<std::mutex> lk(tiles_mtx_);
    for (auto& kv : tiles_) {
        auto& t = kv.second;
        if (!t)
            continue;
        const auto& p = t->position();
        if (((int32_t)p.x >> 4) == cx && ((int32_t)p.z >> 4) == cz) {
            out.push_back(t);
        }
    }
    return out;
}

void World::ensureTilesLoaded(int32_t cx, int32_t cz) {
    uint64_t ckey = packChunkPos(cx, cz);
    {
        std::lock_guard<std::mutex> lk(tiles_mtx_);
        if (tiles_hydrated_chunks_.count(ckey))
            return;
    }
    mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
    if (!c)
        return;
    {
        std::lock_guard<std::mutex> lk(tiles_mtx_);
        if (tiles_hydrated_chunks_.count(ckey))
            return; // Another thread won.
        tiles_hydrated_chunks_.insert(ckey);
    }

    if (c->tile_entities && c->tile_entity_count > 0) {
        for (size_t i = 0; i < c->tile_entity_count; ++i) {
            const uint8_t* blob = c->tile_entities[i];
            size_t blen = c->tile_entity_lens ? c->tile_entity_lens[i] : 0;
            if (!blob || blen == 0)
                continue;
            mcp_tag_t* tag = mcp_nbt_read_bytes(blob, blen, /*little_endian=*/true);
            if (!tag)
                continue;

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
                tile = std::make_shared<cursestone::tile::TileFurnace>(pos);
            }
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

void World::hydrateTilesFromBlocks(int32_t cx, int32_t cz, mcp_chunk_t* c) {
    using cursestone::tile::createTileForBlock;
    using cursestone::tile::isTileBlock;

    const int32_t baseX = cx * 16, baseZ = cz * 16;
    std::vector<std::shared_ptr<cursestone::tile::Tile>> fresh;
    for (int32_t y = 0; y < 128; ++y)
        for (int32_t lz = 0; lz < 16; ++lz)
            for (int32_t lx = 0; lx < 16; ++lx) {
                const uint8_t id = mcp_chunk_get_block_id(c, lx, y, lz);
                if (!isTileBlock(id))
                    continue;
                const int32_t wx = baseX + lx, wz = baseZ + lz;
                {
                    std::lock_guard<std::mutex> lk(tiles_mtx_);
                    if (tiles_.count(packTilePos(wx, y, wz)))
                        continue;
                }
                fresh.push_back(createTileForBlock(
                    id, cursestone::world::Position((double)wx, (double)y, (double)wz)));
            }
    if (fresh.empty())
        return;

    std::lock_guard<std::mutex> lk(tiles_mtx_);
    for (auto& t : fresh)
        if (t)
            tiles_[packTilePos((int32_t)t->position().x, (int32_t)t->position().y,
                               (int32_t)t->position().z)] = std::move(t);
}

void World::syncTilesToChunks() {
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<cursestone::tile::Tile>>> byChunk;
    bool anyDirty = false;
    std::vector<uint64_t> emptiedChunks;
    {
        std::lock_guard<std::mutex> lk(tiles_mtx_);
        for (auto& [_, t] : tiles_) {
            if (!t)
                continue;
            if (t->isDirty())
                anyDirty = true;
            int32_t cx = static_cast<int32_t>(t->position().x) >> 4;
            int32_t cz = static_cast<int32_t>(t->position().z) >> 4;
            byChunk[packChunkPos(cx, cz)].push_back(t);
        }
        for (uint64_t ck : tiles_dirty_chunks_) {
            if (byChunk.find(ck) == byChunk.end())
                byChunk[ck] = {};
        }
        anyDirty = anyDirty || !tiles_dirty_chunks_.empty();
    }
    if (!anyDirty)
        return; // Nothing changed since the last save.

    for (auto& [ckey, tilesInChunk] : byChunk) {
        bool chunkHasDirty = false;
        for (auto& t : tilesInChunk)
            if (t->isDirty()) {
                chunkHasDirty = true;
                break;
            }
        bool emptied = false;
        {
            std::lock_guard<std::mutex> lk(tiles_mtx_);
            emptied = tiles_dirty_chunks_.count(ckey) != 0;
        }
        if (!chunkHasDirty && !emptied)
            continue;

        int32_t cx = static_cast<int32_t>(ckey >> 32);
        int32_t cz = static_cast<int32_t>(ckey & 0xFFFFFFFFULL);
        mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
        if (!c)
            continue; // Chunk unloaded; retry on the next save.

        mcp_chunk_clear_tile_entities(c);
        for (auto& t : tilesInChunk) {
            mcp_tag_t* tag = mcp_compound_tag_new();
            t->writeNBT(tag);
            size_t blen = 0;
            uint8_t* bbuf = mcp_nbt_write_bytes(tag, /*little_endian=*/true, &blen);
            if (bbuf && blen > 0) {
                mcp_chunk_add_tile_entity(c, bbuf, blen);
                t->clearDirty(); // Clear only after writing to the chunk.
            }
            free(bbuf);
            mcp_tag_destroy(tag);
        }
        c->dirty = true; // Ensure flushDirty() saves this chunk.
        if (emptied) {
            std::lock_guard<std::mutex> lk(tiles_mtx_);
            tiles_dirty_chunks_.erase(ckey);
        }
    }
}

void World::evictTilesForChunk(int32_t cx, int32_t cz) {
    uint64_t ckey = packChunkPos(cx, cz);

    std::vector<std::shared_ptr<cursestone::tile::Tile>> tilesInChunk;
    {
        std::lock_guard<std::mutex> lk(tiles_mtx_);
        for (auto it = tiles_.begin(); it != tiles_.end();) {
            if (!it->second) {
                it = tiles_.erase(it);
                continue;
            }
            int32_t tcx = static_cast<int32_t>(it->second->position().x) >> 4;
            int32_t tcz = static_cast<int32_t>(it->second->position().z) >> 4;
            if (tcx == cx && tcz == cz) {
                tilesInChunk.push_back(it->second);
                it = tiles_.erase(it); // Remove from the registry.
            } else {
                ++it;
            }
        }
        tiles_hydrated_chunks_.erase(ckey); // Rehydrate on the next load.
    }
    if (tilesInChunk.empty())
        return;

    bool anyDirty = false;
    for (auto& t : tilesInChunk)
        if (t->isDirty()) {
            anyDirty = true;
            break;
        }
    if (!anyDirty)
        return; // Nothing to flush.

    mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
    if (!c)
        return; // Chunk is gone; unsaved tile data is lost.

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
    c->dirty = true; // Ensure ChunkManager saves this chunk.
}

int World::unloadDistantChunks(const std::vector<ChunkViewer>& viewers, int32_t margin) {
    auto coords = chunks_.loadedChunkCoords();
    int unloaded = 0;
    for (auto& [cx, cz] : coords) {
        bool needed = false;
        for (auto& v : viewers) {
            int32_t dx = cx - v.cx, dz = cz - v.cz;
            int32_t dist = std::max(std::abs(dx), std::abs(dz));
            if (dist <= v.view_radius + margin) {
                needed = true;
                break;
            }
        }
        if (needed)
            continue;

        evictTilesForChunk(cx, cz); // Flush and remove tiles before unloading the chunk.
        if (chunks_.unloadChunk(cx, cz, /*save=*/true))
            ++unloaded;
    }
    return unloaded;
}

void World::sendWeatherTo(Player* p) {
    if (!p)
        return;
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
    if (state < WEATHER_CLEAR || state > WEATHER_THUNDER)
        state = WEATHER_CLEAR;
    bool changed = (weather_state_ != state);
    weather_state_ = state;
    weather_ticks_left_ = durationTicks > 0 ? durationTicks : -1;
    rain_strength_ = 90000 + (uint32_t)(rand() % 20001);
    thunder_strength_ = 30000 + (uint32_t)(rand() % 10001);
    for (auto& p : Server::getInstance().playersSnapshot()) {
        if (p && p->spawned() && p->getWorld() == this)
            sendWeatherTo(p.get());
    }
    if (changed) {
        static const char* kNames[4] = {"clear", "rain", "rain+thunder", "thunder"};
        utils::MainLogger::get().info(
            std::string("[Weather] World '") + cfg_.name + "' -> " + kNames[state] +
            (weather_ticks_left_ > 0 ? " (" + std::to_string(weather_ticks_left_ / 20) + "s)"
                                     : ""));
    }
}

namespace {

class LightningRemovalTask : public scheduler::Task {
  public:
    LightningRemovalTask(int64_t eid, World* world) : eid_(eid), world_(world) {
    }
    void onRun() override {
        auto rm = std::make_shared<network::protocol::RemoveEntityPacket>();
        rm->eid = eid_;
        for (auto& p : Server::getInstance().playersSnapshot()) {
            if (p && p->spawned() && p->getWorld() == world_)
                p->sendPacket(rm);
        }
    }

  private:
    int64_t eid_;
    World* world_; // World where the lightning struck.
};

} // namespace

void World::spawnLightning(double x, double y, double z, bool withFire) {
    int64_t eid = Server::getInstance().allocateEid();

    auto add = std::make_shared<network::protocol::AddEntityPacket>();
    add->eid = eid;
    add->type = network::protocol::AddEntityPacket::TYPE_LIGHTNING;
    add->x = (float)x;
    add->y = (float)y;
    add->z = (float)z;
    auto exp = std::make_shared<network::protocol::ExplodePacket>();
    exp->x = (float)x;
    exp->y = (float)y;
    exp->z = (float)z;
    exp->radius = 10.0f;
    for (auto& p : Server::getInstance().playersSnapshot()) {
        if (p && p->spawned() && p->getWorld() == this) {
            p->sendPacket(add);
            p->sendPacket(exp);
        }
    }

    // Despawn (: age > 20 ticks).
    scheduler::ServerScheduler::get().scheduleSync(
        std::make_shared<LightningRemovalTask>(eid, this), 20);

    if (Server::getInstance().difficulty() > 0) {
        for (auto& p : Server::getInstance().playersSnapshot()) {
            if (!p || !p->spawned())
                continue;
            if (p->getWorld() != this)
                continue; // Same world.
            const auto& pos = p->getNextPosition();
            if (std::fabs(pos.x - x) > 4.0 || std::fabs(pos.y - y) > 3.0 ||
                std::fabs(pos.z - z) > 4.0)
                continue;
            p->attack(8.0f + (float)(std::rand() % 13));
            p->setOnFire(3 + std::rand() % 6);
        }
    }

    if (withFire) {
        int32_t bx = (int32_t)std::floor(x);
        int32_t bz = (int32_t)std::floor(z);
        int32_t by = (int32_t)std::floor(y);
        if (by >= 0 && by <= 255 && isChunkLoaded(bx, bz)) {
            uint8_t at = getBlockId(bx, by, bz);
            uint8_t below = by > 0 ? getBlockId(bx, by - 1, bz) : static_cast<uint8_t>(block::AIR);
            bool below_liquid = (below == block::WATER || below == block::FLOWING_WATER ||
                                 below == block::LAVA || below == block::FLOWING_LAVA);
            if ((at == block::AIR || at == block::TALL_GRASS) && !below_liquid) {
                setBlock(bx, by, bz, block::FIRE, 0);
                auto up = std::make_shared<network::protocol::UpdateBlockPacket>();
                up->x = bx;
                up->z = bz;
                up->y = (uint8_t)by;
                up->blockId = block::FIRE;
                up->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
                for (auto& p : Server::getInstance().playersSnapshot()) {
                    if (p && p->spawned() && p->getWorld() == this)
                        p->sendPacket(up);
                }
            }
        }
    }
}

void World::strikeRandomPlayerLightning() {
    auto players = Server::getInstance().playersSnapshot();
    Player* target = nullptr;
    for (auto& p : players) {
        if (p && p->spawned() && p->getWorld() == this) {
            target = p.get();
            break;
        }
    }
    if (!target)
        return;

    const auto& tpos = target->getNextPosition();
    int32_t bx = (int32_t)std::floor(tpos.x) + (std::rand() % 129 - 64);
    int32_t bz = (int32_t)std::floor(tpos.z) + (std::rand() % 129 - 64);
    if (!isChunkLoaded(bx, bz))
        return; // The strike landed outside the loaded world.
    mcp_chunk_t* c = chunks_.getChunkIfLoaded(bx >> 4, bz >> 4);
    if (!c || !c->height_map)
        return;
    // height_map = topmost light-blocking block + 1 => first AIR and.
    int32_t y = c->height_map[((bz & 15) << 4) | (bx & 15)];
    if (y <= 0 || y > 255)
        return;
    spawnLightning((double)bx + 0.5, (double)y, (double)bz + 0.5, /*withFire=*/true);
}

void World::doWeatherTick(uint64_t current_tick) {
    // 1. Duration countdown.
    if (weather_ticks_left_ > 0) {
        if (--weather_ticks_left_ <= 0)
            setWeather(WEATHER_CLEAR);
    }

    if (isThundering() && current_tick >= next_lightning_tick_) {
        next_lightning_tick_ = current_tick + LIGHTNING_STRIKE_INTERVAL;
        strikeRandomPlayerLightning();
    }

    if (!isRaining())
        return;

    // 2. Fire sweep (only while raining).
    auto coords = chunks_.loadedChunkCoords();
    if (coords.empty())
        return;
    int extinguished = 0;
    for (int i = 0; i < WEATHER_CHUNKS_PER_TICK && extinguished < MAX_EXTINGUISH_PER_TICK; ++i) {
        const auto& [cx, cz] = coords[(rain_scan_offset_ + (uint64_t)i) % coords.size()];
        mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
        if (!c)
            continue;

        for (uint8_t sy = 0; sy < 16; ++sy) {
            mcp_chunk_section_t* sec = c->sections[sy];
            if (!sec || !sec->blocks)
                continue;
            if (!std::memchr(sec->blocks, (int)block::FIRE, 4096))
                continue;

            for (int j = 0; j < 4096 && extinguished < MAX_EXTINGUISH_PER_TICK; ++j) {
                if (sec->blocks[j] != (uint8_t)block::FIRE)
                    continue;
                int lx = j & 15;
                int lz = (j >> 4) & 15;
                int32_t y = ((int32_t)sy << 4) | (j >> 8);
                int32_t h = c->height_map ? c->height_map[(lz << 4) | lx] : 256;
                if (y < h)
                    continue;
                int32_t wx = (cx << 4) | lx;
                int32_t wz = (cz << 4) | lz;
                setBlock(wx, y, wz, block::AIR, 0);
                auto up = std::make_shared<network::protocol::UpdateBlockPacket>();
                up->x = wx;
                up->z = wz;
                up->y = (uint8_t)y;
                up->blockId = block::AIR;
                up->blockData = 0;
                up->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
                for (auto& p : Server::getInstance().playersSnapshot()) {
                    if (p && p->spawned() && p->getWorld() == this)
                        p->sendPacket(up);
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
        utils::MainLogger::get().info("[Weather] Rain extinguished " +
                                      std::to_string(extinguished) + " fire block(s) in level '" +
                                      cfg_.name + "'");
    }
}

} // namespace cursestone::world
