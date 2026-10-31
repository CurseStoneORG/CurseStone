/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  |__ \  __/\__/ / || (_) | | | |  __/
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
#include "item/ItemIds.h"
#include "item/Item.h"
#include "world/World.h"
#include "world/format/Chunk.h"
#include "network/protocol/UpdateBlockPacket.h"
#include "network/protocol/LevelEventPacket.h"
#include "redstone/RedstoneManager.h"
#include "world/Explosion.h"
#include "item/ItemConstants.h"
#include "item/ItemFlintSteel.h"
#include "block/BlockIds.h"
#include "block/BlockOrientation.h"
#include "block/Block.h"
#include "block/LiquidFlow.h"
#include "tile/TileSign.h"
#include "tile/TileChest.h"
#include "tile/TileFurnace.h"
#include "inventory/FurnaceInventory.h"
#include "inventory/PlayerInventory.h"
#include "event/EventManager.h"
#include "event/block/BlockBreakEvent.h"
#include "event/block/BlockPlaceEvent.h"
#include "event/player/PlayerInteractEvent.h"
#include "utils/TextColor.h"
#include "utils/MainLogger.hpp"
#include <cmath>
#include <utility>

namespace cursestone {

// Checks whether a chunk is within the player's view distance.
bool Player::canSeeBlock(int32_t cx, int32_t cz) const {
    int32_t pcx = (int32_t)std::floor(pos_.x) >> 4;
    int32_t pcz = (int32_t)std::floor(pos_.z) >> 4;
    int32_t vd = cfg_.view_distance;
    if (vd < 1)
        vd = 1;
    int32_t dx = cx - pcx, dz = cz - pcz;
    if (dx < 0)
        dx = -dx;
    if (dz < 0)
        dz = -dz;
    return dx <= vd && dz <= vd;
}

// Formats a byte as hexadecimal.
static std::string hex8(uint8_t v) {
    static const char* d = "0123456789ABCDEF";
    std::string s = "0x";
    s += d[v >> 4];
    s += d[v & 0x0F];
    return s;
}

// Sends a block update to players who can see its chunk.
static void broadcastBlockChange(cursestone::Server* server, world::World* world, int32_t x,
                                 int32_t y, int32_t z, uint8_t blockId, uint8_t blockData) {
    if (!server || !world)
        return;
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = x;
    ubp->z = z;
    ubp->y = (uint8_t)y;
    ubp->blockId = blockId;
    ubp->blockData = blockData;
    ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
    int32_t cx = x >> 4, cz = z >> 4;
    for (auto& player : server->playersSnapshot()) {
        if (!player)
            continue;
        if (player->getWorld() != world)
            continue; // Same world.
        if (!player->canSeeBlock(cx, cz))
            continue;
        player->sendPacket(ubp);
    }
}

// Defined below handlePlayerAction.
static std::pair<uint16_t, uint16_t> lookupBlockDrop(uint8_t blockId, uint8_t blockData);

static void resyncUseTarget(Player* self, world::World* lvl, int32_t x, int32_t y, int32_t z,
                            uint8_t face) {
    if (!self || !lvl)
        return;
    // Include the clicked block and its adjacent face.
    static const int8_t fx[6] = {0, 0, 0, 0, -1, 1};
    static const int8_t fy[6] = {-1, 1, 0, 0, 0, 0};
    static const int8_t fz[6] = {0, 0, -1, 1, 0, 0};
    const int cells[2][3] = {
        {x, y, z},
        {x + fx[face], y + fy[face], z + fz[face]},
    };
    for (auto& c : cells) {
        int32_t cx = c[0] >> 4, cz = c[2] >> 4;
        if (!lvl->chunks().getChunkIfLoaded(cx, cz))
            continue;
        auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
        ubp->x = c[0];
        ubp->z = c[2];
        ubp->y = (uint8_t)c[1];
        ubp->blockId = lvl->getBlockId(c[0], c[1], c[2]);
        ubp->blockData = lvl->getBlockData(c[0], c[1], c[2]);
        ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
        self->sendPacket(ubp);
    }
    // Restore the held item after a rejected interaction.
    if (self->getInventory()) {
        self->sendSlot(self->getInventory()->heldSlot());
    }
}

// The collision volume a freshly placed block would fill, in world
// space. Blocks without an entity collision volume (air, liquids,
// plants, torches, rails...) come back empty and never refuse a
// placement, matching the collision cubes movement runs on.
static mcp_aabb_t placementCollisionBox(int x, int y, int z, uint8_t blockId) {
    if (!world::World::entityCollidable(blockId))
        return mcp_aabb_make(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    mcp_aabb_t local = mcp_aabb_make(0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
    if (block::Block* def = block::Block::get(blockId)) {
        mcp_aabb_t bb = def->getBoundingBox();
        if (bb.maxX > bb.minX && bb.maxY > bb.minY && bb.maxZ > bb.minZ)
            local = bb;
    }
    return mcp_aabb_make(x + local.minX, y + local.minY, z + local.minZ,
                         x + local.maxX, y + local.maxY, z + local.maxZ);
}

// A placement whose collision volume overlaps the hitbox of an entity
// that blocks building is refused. Players count with their full body
// (0.6 wide, 1.8 tall) — not just the feet cell — so no block can end
// up inside the space a player or mob occupies.
static bool placementOverlapsEntity(world::World* lvl, Server* srv, const mcp_aabb_t& box) {
    if (!(box.maxX > box.minX && box.maxY > box.minY && box.maxZ > box.minZ))
        return false; // No collision volume, nothing to overlap.
    if (!srv)
        return false;
    for (auto& other : srv->playersSnapshot()) {
        if (!other || !other->spawned() || other->isClosed())
            continue;
        if (other->getWorld() != lvl)
            continue;
        if (mcp_aabb_intersects(other->getBoundingBox(), box))
            return true;
    }
    return false;
}

// Vanilla item-entity spawn scatter (handheld Entity::drop: a uniform
// direction over the full circle with speed nextFloat() * 0.2 + 0.1, and
// the standard 0.2 upward pop). Container spills use it so every stack
// lands as its own entity instead of stacking in one column.
static void scatterDropMotion(double& mx, double& my, double& mz) {
    auto lcg = []() {
        return (double)std::rand() / (double)RAND_MAX;
    };
    const double angle = lcg() * 2.0 * 3.14159265358979323846;
    const double speed = lcg() * 0.2 + 0.1;
    mx = -std::sin(angle) * speed;
    my = 0.2;
    mz = std::cos(angle) * speed;
}

void Player::handleBlockBreak(int x, int y, int z) {
    if (!spawned_.load(std::memory_order_relaxed))
        return;
    auto* lvl = getWorld();
    if (!lvl)
        return;

    // Reject invalid world heights.
    if (y < 0 || y >= 256)
        return;

    // Get the chunk containing this block. Use the loaded-chunk lookup:
    // getChunk() here would synchronously generate (and later save) chunks
    // at any attacker-chosen coordinate from a RemoveBlockPacket.
    int32_t cx = x >> 4;
    int32_t cz = z >> 4;
    mcp_chunk_t* ch = lvl->chunks().getChunkIfLoaded(cx, cz);
    if (!ch)
        return;

    int localX = x & 15;
    int localZ = z & 15;
    uint8_t blockId = mcp_chunk_get_block_id(ch, localX, y, localZ);
    uint8_t blockData = mcp_chunk_get_block_data(ch, localX, y, localZ);

    // Bedrock cannot be broken.
    if (blockId == 7)
        return;
    // Resync air blocks to prevent client-side ghost blocks.
    if (blockId == 0) {
        auto ghost = std::make_shared<network::protocol::UpdateBlockPacket>();
        ghost->x = x;
        ghost->z = z;
        ghost->y = (uint8_t)y;
        ghost->blockId = 0;
        ghost->blockData = 0;
        ghost->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
        if (srv_) {
            for (auto& other : srv_->playersSnapshot()) {
                if (!other)
                    continue;
                if (other->getWorld() != lvl)
                    continue;
                if (!other->canSeeBlock(cx, cz))
                    continue;
                if (!other->spawned())
                    continue;
                other->sendPacket(ghost);
            }
        }
        return;
    }

    // Allow plugins to cancel block breaking.
    {
        auto ev = event::EventManager::get().fire<event::block::BlockBreakEvent>(
            this, math::Vector3{(double)x, (double)y, (double)z}, (int)blockId);
        if (ev.isCancelled())
            return;
    }

    int pairX = 0, pairY = 0, pairZ = 0;
    bool hasPair = false;
    uint16_t forcedDropId = 0, forcedDropMeta = 0;
    {
        static const int8_t bedDirX[4] = {0, -1, 0, 1}; // South, west, north, east.
        static const int8_t bedDirZ[4] = {1, 0, -1, 0};
        switch (blockId) {
        case block::WOODEN_DOOR:
        case block::IRON_DOOR: {
            forcedDropId =
                (uint16_t)((blockId == block::WOODEN_DOOR) ? item::WOODEN_DOOR // Door item.
                                                           : item::IRON_DOOR); // Iron door item.
            forcedDropMeta = 0;
            bool isTop = (blockData & 0x08) != 0;
            pairX = x;
            pairZ = z;
            pairY = isTop ? (y - 1) : (y + 1);
            hasPair = (pairY >= 0 && pairY < 256);
            break;
        }
        case block::BED_BLOCK: {
            forcedDropId = (uint16_t)item::BED; // Bed item.
            forcedDropMeta = 0;
            bool isHead = (blockData & 0x08) != 0;
            uint8_t dir = blockData & 0x03;
            pairX = x + (isHead ? -bedDirX[dir] : bedDirX[dir]);
            pairZ = z + (isHead ? -bedDirZ[dir] : bedDirZ[dir]);
            pairY = y;
            hasPair = true;
            // Wake players sleeping in this bed.
            Player::wakeSleepersOnBed(srv_, lvl, x, y, z);
            // A broken bed can no longer be a respawn point: every player
            // whose spawn was this bed falls back to the world spawn.
            Player::clearSpawnsOnBed(srv_, lvl, x, y, z);
            break;
        }
        default:
            break;
        }
    }

    // Replace the block with air.
    mcp_chunk_set_block_full(ch, localX, y, localZ, 0, 0);
    mcp_chunk_recalculate_height_map(ch);
    ch->dirty = true;

    // Remove associated block entities to prevent stale data. A broken
    // chest first releases its vanilla pairing and hands out its OWN 27
    // slots as item entities — the paired half keeps its items (no
    // duplication), and every open GUI on the pair is closed.
    if (blockId == block::CHEST) {
        auto chestTile = std::dynamic_pointer_cast<tile::TileChest>(lvl->getTile(x, y, z));
        if (chestTile) {
            auto chestPair = chestTile->pairTile(lvl);
            if (srv_) {
                for (auto& viewer : srv_->playersSnapshot()) {
                    if (!viewer)
                        continue;
                    if (viewer->open_chest_ == chestTile ||
                        (chestPair && viewer->open_chest_ == chestPair)) {
                        viewer->closeOpenChest();
                    }
                }
            }
            // Vanilla ContainerBlock::breakBlock — the container's slots
            // spill as item entities in EVERY game mode. Creative only
            // suppresses the block item itself (the generic drop at the
            // end of this handler), never the contents.
            auto contents = chestTile->inventory()->getContents();
            for (const auto& entry : contents) {
                const auto& stack = entry.second;
                if (stack.isEmpty())
                    continue;
                double mx, my, mz;
                scatterDropMotion(mx, my, mz);
                dropItem(x + 0.5, y + 0.5, z + 0.5, stack.id, (uint8_t)stack.count,
                         stack.meta, mx, my, mz, 10, stack.nbt);
            }
            if (chestPair) {
                chestPair->clearPair();
                // Respawn the surviving half's NBT WITHOUT the pair tags
                // so every client un-renders the double chest.
                const auto& pp = chestPair->position();
                sendBlockEntityData((int32_t)pp.x, (int32_t)pp.y, (int32_t)pp.z, *chestPair,
                                    nullptr);
            }
            chestTile->clearPair();
            lvl->removeTile(x, y, z);
        }
    } else if (blockId == block::FURNACE || blockId == block::BURNING_FURNACE ||
               blockId == block::SIGN_POST || blockId == block::WALL_SIGN) {
        // Vanilla FurnaceBlock::breakBlock — input, fuel and output spill
        // in every game mode, like every other container.
        if (blockId == block::FURNACE || blockId == block::BURNING_FURNACE) {
            if (auto furnaceTile =
                    std::dynamic_pointer_cast<tile::TileFurnace>(lvl->getTile(x, y, z))) {
                auto contents = furnaceTile->inventory()->getContents();
                for (const auto& entry : contents) {
                    const auto& stack = entry.second;
                    if (stack.isEmpty())
                        continue;
                    double mx, my, mz;
                    scatterDropMotion(mx, my, mz);
                    dropItem(x + 0.5, y + 0.5, z + 0.5, stack.id, (uint8_t)stack.count,
                             stack.meta, mx, my, mz, 10, stack.nbt);
                }
            }
        }
        lvl->removeTile(x, y, z);
    }

    // Update neighboring redstone components.
    cursestone::redstone::notifyNeighbors(lvl, srv_, x, y, z);

    // Wake liquid neighbors so the removed cell floods vanilla-style.
    cursestone::block::LiquidFlow::onBlockChanged(lvl, x, y, z);

    // Update falling blocks above the removed block.
    lvl->checkFallingSupport(x, y + 1, z);

    // Prepare the block update packet.
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = x;
    ubp->z = z;
    ubp->y = (uint8_t)y;
    ubp->blockId = 0;
    ubp->blockData = 0;
    ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;

    // Send the break effect and sound at the block center.
    auto partEv = std::make_shared<network::protocol::LevelEventPacket>();
    partEv->evid = network::protocol::LevelEventPacket::EVENT_PARTICLE_DESTROY;
    partEv->x = (float)x + 0.5f;
    partEv->y = (float)y + 0.5f;
    partEv->z = (float)z + 0.5f;
    partEv->data = ((int32_t)blockId & 0xFF) | ((int32_t)blockData << 12);

    // Send updates to players whose view distance includes this chunk.
    int32_t pk_cx = x >> 4;
    int32_t pk_cz = z >> 4;
    int broadcastTo = 0, broadcastOk = 0;
    if (srv_) {
        for (auto& other : srv_->playersSnapshot()) {
            if (!other)
                continue;
            if (!other->canSeeBlock(pk_cx, pk_cz))
                continue;
            ++broadcastTo;
            if (!other->spawned())
                continue;
            other->sendPacket(ubp);
            other->sendPacket(partEv);
            ++broadcastOk;
        }
    }

    // Remove fire that loses its supporting block.
    if (y + 1 < 256 && lvl->getBlockId(x, y + 1, z) == block::FIRE) {
        lvl->setBlock(x, y + 1, z, 0, 0);
        auto fubp = std::make_shared<network::protocol::UpdateBlockPacket>();
        fubp->x = x;
        fubp->z = z;
        fubp->y = (uint8_t)(y + 1);
        fubp->blockId = 0;
        fubp->blockData = 0;
        fubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
        if (srv_) {
            for (auto& other : srv_->playersSnapshot()) {
                if (!other || other->getWorld() != lvl)
                    continue;
                if (!other->canSeeBlock(cx, cz))
                    continue;
                if (!other->spawned())
                    continue;
                other->sendPacket(fubp);
            }
        }
    }

    // Remove the matching half of doors and beds.
    if (hasPair) {
        uint8_t pairId = lvl->getBlockId(pairX, pairY, pairZ);
        bool pairMatches = false;
        switch (blockId) {
        case block::WOODEN_DOOR:
        case block::IRON_DOOR:
            // Match the same door's opposite half.
            pairMatches = (pairId == blockId) &&
                          ((lvl->getBlockData(pairX, pairY, pairZ) & 0x08) != (blockData & 0x08));
            break;
        case block::BED_BLOCK:
            pairMatches = (pairId == block::BED_BLOCK) &&
                          // Match the opposite bed half.
                          ((lvl->getBlockData(pairX, pairY, pairZ) & 0x08) != (blockData & 0x08));
            break;
        default:
            break;
        }
        if (pairMatches) {
            mcp_chunk_t* pch = lvl->chunks().getChunkIfLoaded(pairX >> 4, pairZ >> 4);
            if (pch) {
                mcp_chunk_set_block_full(pch, pairX & 15, pairY, pairZ & 15, 0, 0);
                mcp_chunk_recalculate_height_map(pch);
                pch->dirty = true;

                auto pubp = std::make_shared<network::protocol::UpdateBlockPacket>();
                pubp->x = pairX;
                pubp->z = pairZ;
                pubp->y = (uint8_t)pairY;
                pubp->blockId = 0;
                pubp->blockData = 0;
                pubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
                int32_t pcx = pairX >> 4, pcz = pairZ >> 4;
                for (auto& other : srv_->playersSnapshot()) {
                    if (!other)
                        continue;
                    if (other->getWorld() != lvl)
                        continue;
                    if (!other->canSeeBlock(pcx, pcz))
                        continue;
                    if (!other->spawned())
                        continue;
                    other->sendPacket(pubp);
                }
                // Update redstone and falling blocks near the removed half.
                cursestone::redstone::notifyNeighbors(lvl, srv_, pairX, pairY, pairZ);
                lvl->checkFallingSupport(pairX, pairY + 1, pairZ);
            }
        }
    }

    // Drop items unless the player is in creative mode.
    bool creative = (cfg_.gamemode & 1) != 0;
    if (!creative) {
        // Apply block-breaking exhaustion.
        addExhaustion(0.025f);
        // Use the item ID for doors and beds.
        auto drop = (forcedDropId != 0) ? std::make_pair(forcedDropId, forcedDropMeta)
                                        : lookupBlockDrop(blockId, blockData);
        if (drop.first != 0) {
            dropItem(x + 0.5, y + 0.5, z + 0.5, drop.first, 1, drop.second);
        }
    }

    if (srv_ && srv_->debugMode()) {
        utils::MainLogger::get().info("[BLOCK] " + cfg_.username + " broke block at " +
                                      std::to_string(x) + "," + std::to_string(y) + "," +
                                      std::to_string(z) + " (was id=" + std::to_string(blockId) +
                                      " data=" + std::to_string(blockData) + ")");
        // Log block-update delivery diagnostics.
        utils::MainLogger::get().info(
            "[BLOCK-SYNC] break at chunk " + std::to_string(pk_cx) + "," + std::to_string(pk_cz) +
            " flags=" + hex8(network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY) +
            " -> in_view=" + std::to_string(broadcastTo) + " sent=" + std::to_string(broadcastOk));
    }
}

namespace {

// Checks whether an ID belongs to a stair block.
bool isStairsId(uint8_t id) {
    switch (id) {
    case 53:
    case 67:
    case 108:
    case 109:
    case 114:
    case 128:
    case 134:
    case 135:
    case 136:
    case 156:
    case 163:
    case 164:
    case 180:
    case 203:
        return true;
    default:
        return false;
    }
}

// Computes block metadata for placement.
uint8_t computeVanillaPlacementMeta(uint8_t blockId, uint8_t baseData, uint8_t face, float yaw,
                                    float hitY) {
    using block::BlockOrientation;
    const uint8_t q = BlockOrientation::quadrant(yaw); // South, west, north, east.

    // Stairs face the player.
    if (isStairsId(blockId)) {
        return BlockOrientation::stairsMeta(q);
    }

    // Slab metadata stores the variant and upper-half flag.
    if (blockId == 44 /*SLAB*/ || blockId == block::SLAB_WOOD || blockId == block::DOUBLE_SLAB_ID ||
        blockId == block::DOUBLE_SLAB_WOOD) {
        return (uint8_t)((baseData & 0x07) | ((face != 0 && hitY > 0.5f) ? 0x08 : 0x00));
    }

    switch (blockId) {
    // Blocks facing the player.
    case block::FURNACE:
    case block::BURNING_FURNACE:
    case block::CHEST:
    case block::DISPENSER:
    case block::DROPPER:
        return BlockOrientation::horizontalFaceMeta(q);

    // Blocks attached to the clicked face.
    case block::LADDER:
    case block::WALL_SIGN:
    case block::STONE_BUTTON:
        return BlockOrientation::ladderMeta(face);
    case block::TORCH:
        return BlockOrientation::torchMeta(face);
    case block::LEVER:
        return BlockOrientation::leverMeta(face, face != 0 && hitY > 0.5f);
    case block::VINE:
        return BlockOrientation::vineMeta(face);

    // Trapdoors use yaw and the upper-half flag.
    case block::TRAPDOOR:
    case block::TRAPDOOR_IRON:
        return (uint8_t)(BlockOrientation::trapdoorMeta(q) |
                         ((face != 0 && hitY > 0.5f) ? 0x04 : 0x00));

    // Rotate the anvil according to yaw.
    case block::ANVIL:
        return BlockOrientation::anvilMeta(q);

    // These blocks face away from the player.
    case block::PUMPKIN:
    case block::LIT_PUMPKIN:
    case block::UNPOWERED_REPEATER:
    case block::POWERED_REPEATER:
        return (uint8_t)((baseData & 0x0C) | BlockOrientation::yawToMetaOpposite(yaw));

    // Set the axis from the clicked face.
    case block::LOG:
    case 157: /* LOG2 */
    case block::QUARTZ_BLOCK:
    case block::HAY_BALE:
        return BlockOrientation::axisMeta(face, baseData);

    // Preserve metadata for blocks without special placement rules.
    default:
        return baseData;
    }
}

// Vanilla double-chest pairing at placement (BlockChest.onBlockAdded →
// checkForSurroundingChests): the chest just placed joins the first
// adjacent single chest it can legally pair with. A legal pair needs the
// two blocks side by side PERPENDICULAR to their facing — two chests
// stacked front-to-back stay separate singles, exactly like vanilla.
// When both facings are legal but differ, the older chest rotates so the
// pair faces the direction the just-placed chest was given (the
// observable vanilla behavior: the last placement decides the pair's
// facing). Returns the paired half, or null when the chest stays single.
std::shared_ptr<tile::TileChest> tryPairPlacedChest(
    cursestone::Server* srv, world::World* lvl, int32_t x, int32_t y, int32_t z,
    const std::shared_ptr<tile::TileChest>& placed, uint8_t placedMeta) {
    // Side index = block metadata direction: 2=north, 3=south, 4=west, 5=east.
    static const int32_t sdx[6] = {0, 0, 0, 0, -1, 1};
    static const int32_t sdz[6] = {0, 0, -1, 1, 0, 0};

    for (int side = 2; side <= 5; ++side) {
        int32_t nx = x + sdx[side];
        int32_t nz = z + sdz[side];
        if (lvl->getBlockId(nx, y, nz) != block::CHEST)
            continue;
        auto other = std::dynamic_pointer_cast<tile::TileChest>(lvl->getTile(nx, y, nz));
        if (!other || other == placed)
            continue;
        if (placed->isPaired() || other->isPaired())
            continue; // never expand an existing double chest

        const bool sideIsX = (side >= 4); // neighbor sits east/west of the placed chest
        const bool faceIsZ = (placedMeta == 2 || placedMeta == 3);
        const bool perpendicular = (faceIsZ == sideIsX);
        if (!perpendicular)
            continue;
        const uint8_t otherMeta = lvl->getBlockData(nx, y, nz);
        const bool otherPerpendicular =
            sideIsX ? (otherMeta == 2 || otherMeta == 3) : (otherMeta == 4 || otherMeta == 5);
        if (!otherPerpendicular)
            continue; // neighbor faces along the pair axis (front-to-back) — stays single
        if (otherMeta != placedMeta) {
            // The pair must share one facing: rotate the older half to the
            // direction the newly placed chest was given.
            lvl->setBlock(nx, y, nz, block::CHEST, placedMeta);
            broadcastBlockChange(srv, lvl, nx, y, nz, block::CHEST, placedMeta);
        }
        placed->setPair(other);
        return other;
    }
    return nullptr;
}

} // namespace

void Player::handleBlockPlace(int x, int y, int z, uint8_t face, int16_t heldItemId,
                              uint8_t heldCount, uint16_t heldDamage, float fx, float fy,
                              float fz) {
    if (!spawned_.load(std::memory_order_relaxed))
        return;
    if (!getWorld())
        return;

    // Face 0xFF represents using an item in the air.
    if (face == 0xFF) {
        handleUseItemAir(heldItemId);
        return;
    }

    // A block-targeted use never starts the bow or eat pose: the client
    // sends this packet for a plain tap on a block too, and a tap must not
    // show the tension animation to other players. The pose only starts
    // from the air-use path (face 0xFF); any block use clears it.
    bool heldIsFood = false;
    if (inv_) {
        item::ItemStack heldNow = inv_->getHeldItem();
        item::Item* heldDef = item::Item::get(heldNow.id);
        heldIsFood = heldDef && heldDef->isFood(heldNow.meta);
    }
    if (face <= 5 && bow_pose_active_ && srv_) {
        bow_start_action_ = -1;
        broadcastBowPose(false);
    }

    // Food/drink never continues into block placement.
    if (heldIsFood && face <= 5)
        return;

    // Throw splash potions when targeting a block.
    if (heldItemId == (int16_t)item::ITEM_SPLASH_POTION) {
        throwSplashPotion();
        return;
    }

    // Calculate the target placement position.
    int placeX = x, placeY = y, placeZ = z;
    switch (face) {
    case 0:
        placeY--;
        break; // Bottom.
    case 1:
        placeY++;
        break; // Top.
    case 2:
        placeZ--;
        break; // North.
    case 3:
        placeZ++;
        break; // South.
    case 4:
        placeX--;
        break; // West.
    case 5:
        placeX++;
        break; // East.
    default:
        return;
    }
    if (placeY < 0 || placeY >= 256)
        return;

    auto* lvl = getWorld();

    // Handle redstone interactions before placement.
    if (!sneaking_ && cursestone::redstone::tryInteract(lvl, srv_, x, y, z, this)) {
        return;
    }

    if (lvl->getBlockId(x, y, z) == 0) {
        resyncUseTarget(this, lvl, x, y, z, face);
        return;
    }

    const bool sneakLocked = sneaking_;

    if (!sneakLocked) {
        item::ItemStack heldStack;
        heldStack.id = (uint16_t)heldItemId;
        heldStack.meta = (uint8_t)(heldDamage & 0xFF);
        heldStack.count = heldCount;
        auto ev = event::EventManager::get().fire<event::player::PlayerInteractEvent>(
            this, event::player::PlayerInteractEvent::RIGHT_CLICK_BLOCK, x, y, z, (int)face,
            heldStack);
        if (ev.isCancelled()) {
            // Restore client state after a cancelled interaction.
            resyncUseTarget(this, lvl, x, y, z, face);
            return;
        }
    }

    // Open chests instead of placing blocks against them.
    if (!sneakLocked && lvl->getBlockId(x, y, z) == block::CHEST) {
        openChestAt(x, y, z);
        return;
    }

    // Open furnaces instead of placing blocks against them.
    if (!sneakLocked && (lvl->getBlockId(x, y, z) == block::FURNACE ||
                         lvl->getBlockId(x, y, z) == block::BURNING_FURNACE)) {
        openFurnaceAt(x, y, z);
        return;
    }

    // Use beds for sleeping when not sneaking.
    if (!sneakLocked && lvl->getBlockId(x, y, z) == block::BED_BLOCK) {
        // Beds explode when used in the Nether.
        if (lvl->dimension() == 1) {
            world::Explosion boom(world::Position((double)x, (double)y, (double)z), 6.0f);
            boom.explode();
            return;
        }

        // Find the head half of the bed.
        uint8_t meta = lvl->getBlockData(x, y, z);
        int headX = x, headY = y, headZ = z;
        if ((meta & 0x08) == 0) {
            static const int dx4[4] = {0, 0, -1, 1}; // North, south, west, east.
            static const int dz4[4] = {-1, 1, 0, 0};
            bool found = false;
            for (int i = 0; i < 4; ++i) {
                int nx = x + dx4[i], nz = z + dz4[i];
                if (lvl->getBlockId(nx, y, nz) == block::BED_BLOCK &&
                    (lvl->getBlockData(nx, y, nz) & 0x08) == 0x08) {
                    headX = nx;
                    headY = y;
                    headZ = nz;
                    found = true;
                    break;
                }
            }
            if (!found) {
                sendMessage(TC::GRAY + "This bed is incomplete");
                return;
            }
        }

        // Touching a bed binds the respawn point to it even during the
        // day (sleeping below re-binds it too); the rule can be turned
        // off with gameplay.bed-spawn in cursestone.yml.
        if (!srv_ || srv_->bedSpawnEnabled()) {
            setSpawn(headX, headY, headZ, /*save*/ true);
            sendMessage(TC::GRAY + "Respawn point set");
        }

        // Sleeping itself is a night-only affordance.
        int32_t t = lvl->time() % world::World::TIME_FULL;
        bool isNight = (t >= world::World::TIME_NIGHT && t < world::World::TIME_SUNRISE);
        if (!isNight) {
            sendMessage(TC::GRAY + "You can only sleep at night");
            return;
        }

        // Attempt to sleep and report occupied beds.
        if (!sleepOn(headX, headY, headZ)) {
            sendMessage(TC::GRAY + "This bed is occupied");
        }
        return;
    }

    if (!sneakLocked && lvl->getBlockId(x, y, z) == block::WORKBENCH && srv_ &&
        srv_->debugMode()) {
        utils::MainLogger::get().info("[CRAFT] " + cfg_.username + " opened crafting table at " +
                                      std::to_string(x) + "," + std::to_string(y) + "," +
                                      std::to_string(z));
    }

    // Determine the block to place from the held item.
    uint8_t placedBlockId = 0; // Zero means no placement.
    uint8_t placedBlockData = 0;
    bool consumeItem = false;    // Whether to consume one item.
    bool skipPlaceSound = false; // Whether to skip the placement sound.
    bool swapBucket = false;     // Whether the held bucket turns empty (pour).
    (void)heldCount;             // Kept for API compatibility.
    (void)fx;
    (void)fz; // Only fy affects placement orientation.

    if (heldItemId == 0) {
        constexpr uint8_t FIRE_ID = 51;
        uint8_t targetId = lvl->getBlockId(x, y, z);
        if (!sneakLocked && targetId == FIRE_ID) {
            // Get the chunk containing the fire.
            int32_t tcx = x >> 4, tcz = z >> 4;
            if (mcp_chunk_t* tch = lvl->chunks().getChunkIfLoaded(tcx, tcz)) {
                int tlx = x & 15, tlz = z & 15;
                mcp_chunk_set_block_full(tch, tlx, y, tlz, 0, 0);
                tch->dirty = true;
                cursestone::redstone::notifyNeighbors(lvl, srv_, x, y, z);
                // Update falling blocks above the removed fire.
                lvl->checkFallingSupport(x, y + 1, z);

                // Broadcast the block update.
                auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
                ubp->x = x;
                ubp->z = z;
                ubp->y = (uint8_t)y;
                ubp->blockId = 0;
                ubp->blockData = 0;
                ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
                if (srv_) {
                    for (auto& other : srv_->playersSnapshot()) {
                        if (!other)
                            continue;
                        if (other->getWorld() != getWorld())
                            continue; // Same world.
                        if (!other->canSeeBlock(tcx, tcz))
                            continue;
                        other->sendPacket(ubp);
                    }
                }
            }
            return;
        }
        // Resync the target when using an empty hand.
        resyncUseTarget(this, lvl, x, y, z, face);
        if (srv_ && srv_->debugMode()) {
            utils::MainLogger::get().info("[BLOCK] " + cfg_.username +
                                          " attempted place with empty hand — ignored");
        }
        return;
    } else if (heldItemId == 193 || heldItemId == 194 || heldItemId == 195 || heldItemId == 196 ||
               heldItemId == 197 || heldItemId == 324 || heldItemId == 330) {

        uint8_t doorId = (heldItemId == 330) ? item::IRON_DOOR_BLOCK : item::DOOR_BLOCK;

        if (placeY + 1 >= 256)
            return; // The upper half would exceed the world height.

        int32_t cxLower = placeX >> 4, czLower = placeZ >> 4;
        mcp_chunk_t* chLower = lvl->chunks().getChunkIfLoaded(cxLower, czLower);
        if (!chLower)
            return; // Do not generate chunks synchronously.
        int localXLower = placeX & 15, localZLower = placeZ & 15;
        uint8_t existingLower = mcp_chunk_get_block_id(chLower, localXLower, placeY, localZLower);
        if (existingLower != 0 &&
            !cursestone::block::LiquidFlow::isReplaceableId(existingLower))
            return; // The lower cell is occupied.

        // Chunks span the full vertical world height.
        uint8_t existingUpper =
            mcp_chunk_get_block_id(chLower, localXLower, placeY + 1, localZLower);
        if (existingUpper != 0 &&
            !cursestone::block::LiquidFlow::isReplaceableId(existingUpper))
            return; // The upper cell is occupied.

        // Neither door half may appear inside an entity hitbox.
        if (placementOverlapsEntity(lvl, srv_,
                                    placementCollisionBox(placeX, placeY, placeZ, doorId)) ||
            placementOverlapsEntity(lvl, srv_,
                                    placementCollisionBox(placeX, placeY + 1, placeZ, doorId)))
            return;

        uint8_t doorFacing =
            block::BlockOrientation::doorMeta(block::BlockOrientation::quadrant(yaw()));

        uint8_t lowerData =
            doorFacing &
            0x03; // Lower-half orientation.
        uint8_t upperData =
            0x08 |
            (doorFacing &
             0x03); // Upper-half flag and orientation.

        mcp_chunk_set_block_full(chLower, localXLower, placeY, localZLower, doorId, lowerData);
        mcp_chunk_set_block_full(chLower, localXLower, placeY + 1, localZLower, doorId, upperData);
        chLower->dirty = true;

        broadcastBlockChange(srv_, lvl, placeX, placeY, placeZ, doorId, lowerData);
        broadcastBlockChange(srv_, lvl, placeX, placeY + 1, placeZ, doorId, upperData);

        // Play the placement sound for doors.
        {
            auto snd = std::make_shared<network::protocol::LevelEventPacket>();
            snd->evid = network::protocol::LevelEventPacket::EVENT_SOUND_BLOCK_PLACE;
            snd->x = (float)placeX;
            snd->y = (float)placeY;
            snd->z = (float)placeZ;
            snd->data = (int32_t)doorId;
            int32_t scx = placeX >> 4, scz = placeZ >> 4;
            for (auto& other : srv_->playersSnapshot()) {
                if (!other)
                    continue;
                if (other->getWorld() != lvl)
                    continue;
                if (!other->canSeeBlock(scx, scz))
                    continue;
                other->sendPacket(snd);
            }
        }

        cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY, placeZ);
        cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY + 1, placeZ);

        if (!(cfg_.gamemode & 1) && inv_) { // Survival mode only.
            int held = inv_->heldSlot();
            auto cur = inv_->getItem(held);
            if (!cur.isEmpty()) {
                if (cur.count > 1) {
                    cur.count -= 1;
                    inv_->setItem(held, cur);
                } else {
                    inv_->clear(held);
                }
            }
        }
        return;
    } else if (heldItemId == item::BED) {

        int32_t cxB = placeX >> 4, czB = placeZ >> 4;
        mcp_chunk_t* chB = lvl->chunks().getChunkIfLoaded(cxB, czB);
        if (!chB)
            return;
        int localXB = placeX & 15, localZB = placeZ & 15;

        // Require solid ground beneath the bed.
        {
            if (placeY == 0)
                return;
            uint8_t belowId = lvl->getBlockId(placeX, placeY - 1, placeZ);
            if (block::Block::get(belowId)->isTransparent())
                return;
        }

        uint8_t bedMeta = block::BlockOrientation::quadrant(yaw());
        static const int8_t headDX[4] = {0, -1, 0, 1}; // South, west, north, east.
        static const int8_t headDZ[4] = {1, 0, -1, 0};
        int headX = placeX + headDX[bedMeta & 0x03];
        int headZ = placeZ + headDZ[bedMeta & 0x03];

        // Ensure the head cell is empty and supported.
        if (headX < -1000000 || headZ < -1000000)
            return; // Guard against invalid coordinates.
        int32_t headCx = headX >> 4, headCz = headZ >> 4;
        mcp_chunk_t* chHead =
            (headCx == cxB && headCz == czB) ? chB : lvl->chunks().getChunkIfLoaded(headCx, headCz);
        if (!chHead)
            return;
        if (lvl->getBlockId(headX, placeY, headZ) != 0 &&
            !cursestone::block::LiquidFlow::isReplaceableId(
                lvl->getBlockId(headX, placeY, headZ)))
            return;
        {
            if (placeY == 0)
                return;
            uint8_t belowHeadId = lvl->getBlockId(headX, placeY - 1, headZ);
            if (block::Block::get(belowHeadId)->isTransparent())
                return;
        }

        // The foot and head cells may not appear inside an entity hitbox.
        if (placementOverlapsEntity(
                lvl, srv_, placementCollisionBox(placeX, placeY, placeZ, block::BED_BLOCK)) ||
            placementOverlapsEntity(
                lvl, srv_, placementCollisionBox(headX, placeY, headZ, block::BED_BLOCK)))
            return;

        // Allow plugins to cancel bed placement.
        {
            auto ev = event::EventManager::get().fire<event::block::BlockPlaceEvent>(
                this, math::Vector3{(double)placeX, (double)placeY, (double)placeZ},
                (int)block::BED_BLOCK);
            if (ev.isCancelled())
                return;
        }

        mcp_chunk_set_block_full(chB, localXB, placeY, localZB, block::BED_BLOCK,
                                 bedMeta & 0x07); // Foot.
        mcp_chunk_set_block_full(chHead, headX & 15, placeY, headZ & 15, block::BED_BLOCK,
                                 (uint8_t)((bedMeta & 0x07) | 0x08)); // Head.
        chB->dirty = true;
        chHead->dirty = true;

        broadcastBlockChange(srv_, lvl, placeX, placeY, placeZ, block::BED_BLOCK, bedMeta & 0x07);
        broadcastBlockChange(srv_, lvl, headX, placeY, headZ, block::BED_BLOCK,
                             (uint8_t)((bedMeta & 0x07) | 0x08));

        // Play the bed placement sound.
        {
            auto snd = std::make_shared<network::protocol::LevelEventPacket>();
            snd->evid = network::protocol::LevelEventPacket::EVENT_SOUND_BLOCK_PLACE;
            snd->x = (float)placeX;
            snd->y = (float)placeY;
            snd->z = (float)placeZ;
            snd->data = (int32_t)block::BED_BLOCK;
            int32_t scx = placeX >> 4, scz = placeZ >> 4;
            for (auto& other : srv_->playersSnapshot()) {
                if (!other)
                    continue;
                if (other->getWorld() != lvl)
                    continue;
                if (!other->canSeeBlock(scx, scz))
                    continue;
                other->sendPacket(snd);
            }
        }

        cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY, placeZ);
        cursestone::redstone::notifyNeighbors(lvl, srv_, headX, placeY, headZ);

        if (!(cfg_.gamemode & 1) && inv_) { // Survival mode only.
            int held = inv_->heldSlot();
            auto cur = inv_->getItem(held);
            if (!cur.isEmpty()) {
                if (cur.count > 1) {
                    cur.count -= 1;
                    inv_->setItem(held, cur);
                } else {
                    inv_->clear(held);
                }
            }
        }
        return;
    } else if (heldItemId == 33 || heldItemId == item::STICKY_PISTON) {
        uint8_t pistonId =
            (heldItemId == item::STICKY_PISTON) ? block::STICKY_PISTON : block::PISTON_ID;

        int32_t cxP = placeX >> 4, czP = placeZ >> 4;
        mcp_chunk_t* chP = lvl->chunks().getChunkIfLoaded(cxP, czP);
        if (!chP)
            return;
        int localXP = placeX & 15, localZP = placeZ & 15;
        uint8_t existingP = mcp_chunk_get_block_id(chP, localXP, placeY, localZP);
        if (existingP != 0 && !cursestone::block::LiquidFlow::isReplaceableId(existingP))
            return;

        // A piston may not appear inside an entity hitbox.
        if (placementOverlapsEntity(lvl, srv_,
                                    placementCollisionBox(placeX, placeY, placeZ, pistonId)))
            return;

        float pitchDeg = pitch();
        uint8_t pistonFacing;
        if (pitchDeg <= -60.0f) {
            pistonFacing = 1; // Point upward.
        } else if (pitchDeg >= 60.0f) {
            pistonFacing = 0; // Point downward.
        } else {
            // Use the player's horizontal facing direction.
            pistonFacing =
                block::BlockOrientation::pistonMeta(block::BlockOrientation::quadrant(yaw()));
        }

        mcp_chunk_set_block_full(chP, localXP, placeY, localZP, pistonId, pistonFacing);
        chP->dirty = true;
        broadcastBlockChange(srv_, lvl, placeX, placeY, placeZ, pistonId, pistonFacing);
        cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY, placeZ);

        if (!(cfg_.gamemode & 1) && inv_) {
            int held = inv_->heldSlot();
            auto cur = inv_->getItem(held);
            if (!cur.isEmpty()) {
                if (cur.count > 1) {
                    cur.count -= 1;
                    inv_->setItem(held, cur);
                } else {
                    inv_->clear(held);
                }
            }
        }
        return;
    } else if (heldItemId == item::SIGN) {
        // Place a standing sign on top or a wall sign on a side.
        if (face == 0)
            return;
        if (face == 1) {
            placedBlockId = block::SIGN_POST; // Standing sign.
            placedBlockData =
                (uint8_t)(((int)std::floor((yaw() + 180.0f) * 16.0f / 360.0f + 0.5f)) & 0x0F);
        } else {
            placedBlockId = block::WALL_SIGN; // Wall sign.
            placedBlockData = block::BlockOrientation::ladderMeta(face);
        }
        consumeItem = !(cfg_.gamemode & 1); // Survival mode only.
    } else if (heldItemId >= 1 && heldItemId <= 255) {
        // Place the block item with the appropriate orientation.
        placedBlockId = static_cast<uint8_t>(heldItemId);
        placedBlockData = computeVanillaPlacementMeta(
            (uint8_t)heldItemId, (uint8_t)(heldDamage & 0x0F), face, yaw(), fy);
        consumeItem = !(cfg_.gamemode & 1); // Survival mode only.
    } else if (heldItemId == 331) {
        // Place redstone dust.
        placedBlockId = 55;
        placedBlockData = 0;
        consumeItem = !(cfg_.gamemode & 1);
    } else if (heldItemId == item::ITEM_BUCKET) {
        // Vanilla ItemBucket on the 0.15 wire: one item id, the aux value
        // picks the variant — 0 empty, 8 water, 10 lava, 1 milk.
        uint8_t bucketAux = heldDamage & 0xFF;
        if (bucketAux == 8 || bucketAux == 10) {
            // Pour: place the liquid block; the shared path's onBlockChanged
            // runs the vanilla onPlace pass (lava solidifies against water)
            // and schedules the first flow tick.
            placedBlockId = (bucketAux == 8) ? 8 : 10;
            placedBlockData = 0;
            skipPlaceSound = true;
            swapBucket = !(cfg_.gamemode & 1);
        } else if (bucketAux == 0) {
            // Scoop a source liquid cell (draining it).
            int32_t scoopX = x, scoopY = y, scoopZ = z;
            uint8_t scoopId = lvl->getBlockId(scoopX, scoopY, scoopZ);
            if (!cursestone::block::LiquidFlow::isLiquidId(scoopId)) {
                scoopX = placeX;
                scoopY = placeY;
                scoopZ = placeZ;
                scoopId = lvl->getBlockId(scoopX, scoopY, scoopZ);
            }
            if (cursestone::block::LiquidFlow::isLiquidId(scoopId) &&
                lvl->getBlockData(scoopX, scoopY, scoopZ) == 0) {
                bool scoopWater = cursestone::block::LiquidFlow::isWaterId(scoopId);
                lvl->setBlock(scoopX, scoopY, scoopZ, 0, 0);
                broadcastBlockChange(srv_, lvl, scoopX, scoopY, scoopZ, 0, 0);
                if (inv_) {
                    int held = inv_->heldSlot();
                    auto cur = inv_->getItem(held);
                    if (cur.count <= 1) {
                        item::ItemStack filled;
                        filled.id = item::ITEM_BUCKET;
                        filled.meta = scoopWater ? 8 : 10;
                        filled.count = 1;
                        inv_->setItem(held, filled);
                    } else {
                        cur.count -= 1;
                        cur.meta = scoopWater ? 8 : 10;
                        inv_->setItem(held, cur);
                    }
                }
            } else {
                resyncUseTarget(this, lvl, x, y, z, face);
            }
            return;
        } else {
            // Milk drinks through the air-use path only.
            resyncUseTarget(this, lvl, x, y, z, face);
            return;
        }
    } else if (sneakLocked && heldItemId > 255) {
        // Prevent item use while sneaking.
        if (srv_ && srv_->debugMode()) {
            utils::MainLogger::get().info("[SNEAK] " + cfg_.username +
                                          " tried to use item id=" + std::to_string(heldItemId) +
                                          " while crouching — blocked (vanilla)");
        }
        resyncUseTarget(this, lvl, x, y, z, face);
        return;
    } else if (heldItemId == 259 && !sneakLocked) {
        // Handle flint and steel activation.
        item::ItemFlintSteel::onActivate(lvl, this, placeX, placeY,
                                         placeZ,  // Target air cell.
                                         x, y, z, // Clicked block.
                                         face);
        return; // Activation already updates the world.
    } else {
        // Reject items that cannot place blocks.
        resyncUseTarget(this, lvl, x, y, z, face);
        if (srv_ && srv_->debugMode()) {
            utils::MainLogger::get().info("[BLOCK] " + cfg_.username + " used non-block item id=" +
                                          std::to_string(heldItemId) + " — no placement");
        }
        return;
    }

    // Place the block at the target position.
    int32_t cx = placeX >> 4;
    int32_t cz = placeZ >> 4;

    mcp_chunk_t* ch = lvl->chunks().getChunkIfLoaded(cx, cz);
    if (!ch) {
        if (srv_ && srv_->debugMode()) {
            utils::MainLogger::get().info(
                "[BLOCK] " + cfg_.username + " tried to place block in unloaded chunk (" +
                std::to_string(cx) + "," + std::to_string(cz) + ") — ignored");
        }
        return;
    }

    int localX = placeX & 15;
    int localZ = placeZ & 15;

    uint8_t existing = mcp_chunk_get_block_id(ch, localX, placeY, localZ);
    constexpr uint8_t FIRE_ID = 51;
    if (existing != 0 && existing != FIRE_ID &&
        !cursestone::block::LiquidFlow::isReplaceableId(existing)) {
        // Vanilla mayPlace: the cell must be air or a replaceable block
        // (water, lava, tall grass, fire...) to accept the placement.
        resyncUseTarget(this, lvl, x, y, z, face);
        return;
    }

    // Vanilla mayPlace also refuses cells whose collision volume would
    // intersect an entity hitbox — the player's own body included, feet
    // and head cells alike. Blocks without a collision volume (liquids,
    // plants, torches...) stay placeable, so a bucket still pours onto
    // the pourer.
    if (placementOverlapsEntity(lvl, srv_,
                                placementCollisionBox(placeX, placeY, placeZ, placedBlockId))) {
        resyncUseTarget(this, lvl, x, y, z, face);
        return;
    }

    // Allow plugins to cancel block placement.
    {
        auto ev = event::EventManager::get().fire<event::block::BlockPlaceEvent>(
            this, math::Vector3{(double)placeX, (double)placeY, (double)placeZ},
            (int)placedBlockId);
        if (ev.isCancelled())
            return;
    }

    // Update the chunk with the placed block.
    mcp_chunk_set_block_full(ch, localX, placeY, localZ, placedBlockId, placedBlockData);
    ch->dirty = true;

    // Vanilla UPDATE_ALL onPlace + neighbor pass for the placed block
    // (liquid placement starts flowing; neighbors of water wake up).
    cursestone::block::LiquidFlow::onBlockChanged(lvl, placeX, placeY, placeZ);

    // Register signs so their text persists after reloading.
    if (placedBlockId == block::SIGN_POST || placedBlockId == block::WALL_SIGN) {
        auto sign = std::make_shared<tile::TileSign>(
            cursestone::world::Position((double)placeX, (double)placeY, (double)placeZ));
        sign->setCreator(cfg_.uuid);
        lvl->setTile(placeX, placeY, placeZ, sign);
    }

    // Register the chest block entity immediately (like vanilla, the
    // block actor exists as soon as the block does) and try to pair it
    // with an adjacent single chest — two chests placed together become
    // one vanilla double chest.
    if (placedBlockId == block::CHEST) {
        auto placedChest = std::make_shared<tile::TileChest>(
            cursestone::world::Position((double)placeX, (double)placeY, (double)placeZ));
        lvl->setTile(placeX, placeY, placeZ, placedChest);
        auto pair = tryPairPlacedChest(srv_, lvl, placeX, placeY, placeZ, placedChest,
                                       placedBlockData);
        if (pair) {
            // Push the pair tags to every viewer so the client renders
            // and treats the two blocks as one large chest right away.
            sendBlockEntityData(placeX, placeY, placeZ, *placedChest, nullptr);
            const auto& pp = pair->position();
            sendBlockEntityData((int32_t)pp.x, (int32_t)pp.y, (int32_t)pp.z, *pair, nullptr);
        }
    }

    // Update neighboring redstone components.
    cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY, placeZ);

    // Consume one item in survival mode; a poured bucket swaps to an
    // empty one instead of being destroyed (vanilla ItemBucket swap).
    if (inv_ && !(cfg_.gamemode & 1) && (consumeItem || swapBucket)) {
        int held = inv_->heldSlot();
        auto cur = inv_->getItem(held);
        if (!cur.isEmpty()) {
            if (swapBucket) {
                if (cur.count > 1) {
                    cur.count -= 1;
                    cur.meta = 0;
                    inv_->setItem(held, cur);
                } else {
                    item::ItemStack empty;
                    empty.id = item::ITEM_BUCKET;
                    empty.meta = 0;
                    empty.count = 1;
                    inv_->setItem(held, empty);
                }
            } else if (cur.count > 1) {
                cur.count -= 1;
                inv_->setItem(held, cur);
            } else {
                inv_->clear(held);
            }
        }
    }

    // Prepare the block update packet.
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = placeX;
    ubp->z = placeZ;
    ubp->y = (uint8_t)placeY;
    ubp->blockId = placedBlockId;
    ubp->blockData = placedBlockData;
    ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;

    // Prepare the block placement sound.
    auto sndEv = std::make_shared<network::protocol::LevelEventPacket>();
    sndEv->evid = network::protocol::LevelEventPacket::EVENT_SOUND_BLOCK_PLACE;
    sndEv->x = (float)placeX;
    sndEv->y = (float)placeY;
    sndEv->z = (float)placeZ;
    sndEv->data = (int32_t)placedBlockId;

    // Send updates to players who can see the chunk.
    int32_t pk_cx = placeX >> 4;
    int32_t pk_cz = placeZ >> 4;
    int placeInView = 0, placeSent = 0;
    if (srv_) {
        for (auto& other : srv_->playersSnapshot()) {
            if (!other)
                continue;
            if (other->getWorld() != getWorld())
                continue; // Same world.
            if (!other->canSeeBlock(pk_cx, pk_cz))
                continue;
            ++placeInView;
            if (!other->spawned())
                continue;
            other->sendPacket(ubp);
            // Fire, water, and lava do not play placement sounds.
            if (!skipPlaceSound)
                other->sendPacket(sndEv);
            ++placeSent;
        }
    }

    // Send sign data after registering and broadcasting the block.
    if (placedBlockId == block::SIGN_POST || placedBlockId == block::WALL_SIGN) {
        auto t = lvl->getTile(placeX, placeY, placeZ);
        if (t)
            sendBlockEntityData(placeX, placeY, placeZ, *t, nullptr);
    }

    lvl->checkFallingSupport(placeX, placeY, placeZ);

    if (srv_ && srv_->debugMode()) {
        utils::MainLogger::get().info(
            "[BLOCK] " + cfg_.username + " placed block at " + std::to_string(placeX) + "," +
            std::to_string(placeY) + "," + std::to_string(placeZ) +
            " (id=" + std::to_string(placedBlockId) + " data=" + std::to_string(placedBlockData) +
            " fromItem=" + std::to_string(heldItemId) + ")");
        // Log block-placement delivery diagnostics.
        utils::MainLogger::get().info(
            "[BLOCK-SYNC] place at chunk " + std::to_string(pk_cx) + "," + std::to_string(pk_cz) +
            " -> in_view=" + std::to_string(placeInView) + " sent=" + std::to_string(placeSent));
    }
}

// Returns the item dropped by a block; {0, 0} means no drop.
static std::pair<uint16_t, uint16_t> lookupBlockDrop(uint8_t blockId, uint8_t blockData) {
    switch (blockId) {
    case 1:
        return {4, blockData & 0x07}; // Stone drops cobblestone.
    case 2:
        return {3, 0}; // Grass drops dirt.
    case 15:
        return {0, 0}; // Iron ore requires smelting.
    case 16:
        return {263, 0}; // Coal ore drops coal.
    case 14:
        return {0, 0}; // Gold ore requires smelting.
    case 21:
        return {351, 4}; // Lapis ore drops lapis lazuli.
    case 56:
        return {264, 0}; // Diamond ore drops diamonds.
    case 73:
        return {331, 0}; // Redstone ore drops redstone.
    case 55:
        return {331, 0}; // Redstone wire drops redstone dust.
    case 76:
        return {75, 0}; // Lit redstone torch drops an unlit torch.
    case 124:
        return {123, 0}; // Lit redstone lamp drops an unlit lamp.
    case 20:
        return {0, 0}; // Glass drops nothing.
    case 79:
        return {0, 0}; // Ice drops nothing.
    case 89:
        return {348, 0}; // Glowstone drops glowstone dust.
    case 18:
        return {0, 0}; // Leaves use chance-based drops.
    case 7:
        return {0, 0}; // Bedrock is unbreakable.
    case 46:
        return {0, 0}; // TNT drops nothing.
    case 51:
        return {0, 0}; // Fire drops nothing.
    case 54:
        return {54, 0}; // Chest drops a chest item.
    case 61:
        return {61, 0}; // Furnace drops a furnace item.
    case 62:
        return {61, 0}; // Lit furnace drops a furnace item.
    case 63:
        return {323, 0}; // Standing sign drops a sign item.
    case 68:
        return {323, 0}; // Wall sign drops a sign item.
    // Two-part blocks drop their corresponding inventory items.
    case 64:
        return {324, 0}; // Wooden door.
    case 71:
        return {330, 0}; // Iron door.
    case 26:
        return {355, 0}; // Bed.
    default:
        return {blockId, blockData}; // Drop the block itself.
    }
}

bool Player::canInteract(math::Vector3 pos, double maxDistance) const {
    double dx = pos.x - pos_.x;
    double dy = pos.y - pos_.y;
    double dz = pos.z - pos_.z;
    return (dx * dx + dy * dy + dz * dz) <= maxDistance * maxDistance;
}
std::vector<item::ItemStack> Player::getDrops() const {
    std::vector<item::ItemStack> drops;
    if (inv_) {
        for (int i = 0; i < inventory::PlayerInventory::SIZE; ++i) {
            auto it = inv_->getItem(i);
            if (!it.isEmpty())
                drops.push_back(it);
        }
    }
    return drops;
}
} // namespace cursestone