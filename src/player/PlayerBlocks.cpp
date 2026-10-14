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
#include "network/protocol/UpdateBlockPacket.h"
#include "network/protocol/LevelEventPacket.h"
#include "redstone/RedstoneManager.h"
#include "item/ItemConstants.h"
#include "item/ItemFlintSteel.h"
#include "block/BlockIds.h"
#include "block/BlockOrientation.h"
#include "tile/TileSign.h"
#include "inventory/PlayerInventory.h"
#include "event/EventManager.h"
#include "event/block/BlockBreakEvent.h"
#include "event/block/BlockPlaceEvent.h"
#include "event/player/PlayerInteractEvent.h"
#include "utils/MainLogger.hpp"
#include <cmath>
#include <utility>

namespace cursestone {

// Ver the nota larga in player.h. Criterio of DISTANCIA, not of bookkeeping:
// "¿this the chunk inside of mi distancia of vision?".
bool Player::canSeeBlock(int32_t cx, int32_t cz) const {
    int32_t pcx = (int32_t)std::floor(pos_.x) >> 4;
    int32_t pcz = (int32_t)std::floor(pos_.z) >> 4;
    int32_t vd  = cfg_.view_distance;
    if (vd < 1) vd = 1;
    int32_t dx = cx - pcx, dz = cz - pcz;
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    return dx <= vd && dz <= vd;
}

// Helper: "0x%02X" of a byte, for the logs of diagnostic.
static std::string hex8(uint8_t v) {
    static const char* d = "0123456789ABCDEF";
    std::string s = "0x";
    s += d[v >> 4];
    s += d[v & 0x0F];
    return s;
}

// Helper: transmite a change of block puntual to all the players
// that are viendo that chunk. is uses for componentes of redstone that
// cambian more of a block (for example, puertas).
static void broadcastBlockChange(cursestone::Server* server, world::World* world,
                                   int32_t x, int32_t y, int32_t z,
                                   uint8_t blockId, uint8_t blockData) {
    if (!server || !world) return;
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = x;
    ubp->z = z;
    ubp->y = (uint8_t)y;
    ubp->blockId = blockId;
    ubp->blockData = blockData;
    ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
    int32_t cx = x >> 4, cz = z >> 4;
    for (auto& player : server->playersSnapshot()) {
        if (!player) continue;
        if (player->getWorld() != world) continue;   // same world
        if (!player->canSeeBlock(cx, cz)) continue;
        player->sendPacket(ubp);
    }
}

// Forward decl — defined below handlePlayerAction.
static std::pair<uint16_t, uint16_t> lookupBlockDrop(uint8_t blockId, uint8_t blockData);

void Player::handleBlockBreak(int x, int y, int z) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    auto* lvl = getWorld();
    if (!lvl) return;

    // Reject out-of-world and values.
    if (y < 0 || y >= 256) return;

    // Get the chunk containing this block.
    int32_t cx = x >> 4;
    int32_t cz = z >> 4;
    mcp_chunk_t* ch = lvl->chunks().getChunk(cx, cz);
    if (!ch) return;

    int localX = x & 15;
    int localZ = z & 15;
    uint8_t blockId   = mcp_chunk_get_block_id  (ch, localX, y, localZ);
    uint8_t blockData = mcp_chunk_get_block_data(ch, localX, y, localZ);

    // Don't break bedrock (id=7).
    if (blockId == 7) return;
    // Already air — nothing to do.
    if (blockId == 0) return;

    // BlockBreakEvent — a listener can cancelarlo (p.ej. protección
    // of zonas) for impedir the breakage without touch the rest of the pipeline.
    {
        auto ev = event::EventManager::get().fire<event::block::BlockBreakEvent>(
            this, math::Vector3{(double)x, (double)y, (double)z}, (int)blockId);
        if (ev.isCancelled()) return;
    }

    // Set block to air (id=0, data=0).
    mcp_chunk_set_block_full(ch, localX, y, localZ, 0, 0);
    mcp_chunk_recalculate_height_map(ch);
    ch->dirty = true;

    // Block-entity cleanup: if the block broken had a tile (chest, furnace,
    // sign) there is that sacarlo of the registro. if not is does, to the re-place
    // something in that same position the tile viejo "revive" with its contenido/
    // state anterior — the same tipo of bug of persistencia that the of the
    // sign. PMMP it resolves in tile::onBreak / World::useBreakOn.
    if (blockId == block::CHEST || blockId == block::FURNACE ||
        blockId == block::BURNING_FURNACE ||
        blockId == block::SIGN_POST || blockId == block::WALL_SIGN) {
        lvl->removeTile(x, y, z);
    }

    // Redstone: breaking to block can disconnect to circuit or remove to support.
    cursestone::redstone::notifyNeighbors(lvl, srv_, x, y, z);

    // Broadcast UpdateBlockPacket.
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = x;
    ubp->z = z;
    ubp->y = (uint8_t)y;
    ubp->blockId = 0;
    ubp->blockData = 0;
    ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;

    // PMMP (protocol 84) sends to SINGLE LevelEventPacket with
    // evid = EVENT_PARTICLE_DESTROY (2001) which acts as BOTH the break
    // particle AND the break sound at the client (per the doc-comment in
    // pocketmine/level/particle/DestroyBlockParticle.php: "NOTE: This is
    // also the destroy block SOUND.").
    //
    // Position: block CENTER (x+0.5, and+0.5, z+0.5).
    // Data:     blockId | (blockMeta << 12)  — note the << 12 shift.
    auto partEv = std::make_shared<network::protocol::LevelEventPacket>();
    partEv->evid = network::protocol::LevelEventPacket::EVENT_PARTICLE_DESTROY;
    partEv->x = (float)x + 0.5f;
    partEv->y = (float)y + 0.5f;
    partEv->z = (float)z + 0.5f;
    partEv->data = ((int32_t)blockId & 0xFF) | ((int32_t)blockData << 12);

    // Broadcast only to players who can SEE this chunk — PMMP
    // `World::getChunkPlayers()`. Deliberately NOT isTrackingChunk():
    // see the note on canSeeBlock() — used_chunks_ is send bookkeeping
    // that gets cleared by teleport/spawn-ACK and goes empty while the
    // client is still rendering, which silently dropped every block
    // change for those players (the "ghost block" bug).
    int32_t pk_cx = x >> 4;
    int32_t pk_cz = z >> 4;
    int broadcastTo = 0, broadcastOk = 0;
    if (srv_) {
        for (auto& other : srv_->playersSnapshot()) {
            if (!other) continue;
            if (!other->canSeeBlock(pk_cx, pk_cz)) continue;
            ++broadcastTo;
            if (!other->spawned()) continue;
            other->sendPacket(ubp);
            other->sendPacket(partEv);
            ++broadcastOk;
        }
    }

    // ---- Drops (PMMP `Level::useBreakOn` -> `Level::dropItem`) ----
    // PMMP: only survival players get drops. Creative players skip.
    //
    // bug fixed ("break a block of part upper mine and the item
    // that cae of above not it puedo pick up"): the drop is generaba in the
    // ESQUINA of the block (x, and, z) — the reference of Genisys/PMMP 0.15
    // (Level::useBreakOn -> $this->dropItem($vector->add(0.5, 0.5, 0.5),
    // $drop)) is the CENTRO of the block. the esquina is the cell shared
    // by the 8 blocks vecinos: the AABB of the item (half-width 0.125)
    // spawns surrounded of blocks solids (the wall of the block broken, the ground
    // vecino...), and to the fall a item from above the server it patea with
    // checkObstruction() using std::rand() — the client it patea with its
    // own logica/semilla, this way that ambos terminan simulando caidas
    // DISTINTAS: tu caminas hacia the item that VES and the server it has in
    // other lado, outside of the volumen of recoleccion. Naciendo in the centro
    // of the block (cell 100% of air tras the breakage) not there is obstruction
    // ni divergencia: the item cae to the same place in the server and in the
    // client, and the pickup works to the 100%.
    bool creative = (cfg_.gamemode & 1) != 0;
    if (!creative) {
        auto drop = lookupBlockDrop(blockId, blockData);
        if (drop.first != 0) {
            dropItem(x + 0.5, y + 0.5, z + 0.5, drop.first, 1, drop.second);
        }
    }

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[BLOCK] " + cfg_.username + " broke block at " +
            std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) +
            " (was id=" + std::to_string(blockId) + " data=" + std::to_string(blockData) + ")");
        // diagnostic of the bug "the other not ven the block broken":
        // how many players remain inside of the area of vision of the chunk
        // and to how many actually to them mando the UpdateBlockPacket. if
        // esto dice sent=0 with area>0, the packet is lost in the path;
        // if area=0, the filtro of distancia not yet cubre to the receptor.
        utils::MainLogger::get().info("[BLOCK-SYNC] break at chunk " +
            std::to_string(pk_cx) + "," + std::to_string(pk_cz) +
            " flags=" + hex8(network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY) +
            " -> in_view=" + std::to_string(broadcastTo) +
            " sent=" + std::to_string(broadcastOk));
    }
}

namespace {

// isStairsId — escaleras of MCPE 0.15 / protocolo 84. Cubre the of
// BlockIds.h (53, 67, 108, 109, 114, 156) more the variantes of madera
// that exist in the familia (128, 134-136, 163, 164, 180, 203) by if
// is are registered after; a id extra here not affects to nothing more.
bool isStairsId(uint8_t id) {
    switch (id) {
        case 53: case 67: case 108: case 109: case 114:
        case 128: case 134: case 135: case 136:
        case 156: case 163: case 164: case 180: case 203:
            return true;
        default:
            return false;
    }
}

// computeVanillaPlacementMeta — metadata of COLOCACIÓN vanilla for the
// block recién puesto, with the same reference that uses everything the
// proyecto for the behavior of protocolo 84: Nukkit. the tablas
// of orientación live in block/BlockOrientation.h (only source of
// truth; before each block had its tabla emits). only orienta the
// blocks that in vanilla it hacen; the rest conserva baseData tal which.
//
//   face : cara clicked (0=bottom..5=east) of the UseItemPacket
//   yaw  : yaw of the player in the momento of place
//   hitY : componente and of the point of impacto (0..1) — sirve for slabs
//          (half upper) and trampillas (montada above)
uint8_t computeVanillaPlacementMeta(uint8_t blockId, uint8_t baseData,
                                    uint8_t face, float yaw, float hitY) {
    using block::BlockOrientation;
    const uint8_t q = BlockOrientation::quadrant(yaw); // 0=S 1=W 2=N 3=E

    // Escaleras: always miran hacia the player (Nukkit BlockStairs.place,
    // faces {2,1,3,0}). the esquinas/uniones the calcula the client to
    // partir of the meta of the vecinos — the server only saves the 2 bits.
    if (isStairsId(blockId)) {
        return BlockOrientation::stairsMeta(q);
    }

    // Slabs / doubleslabs: bits bajos = variante, bit 0x08 = half upper
    // when the click apunta to the half alta of the lado of the cara.
    if (blockId == 44 /*SLAB*/ || blockId == block::SLAB_WOOD ||
        blockId == block::DOUBLE_SLAB_ID || blockId == block::DOUBLE_SLAB_WOOD) {
        return (uint8_t)((baseData & 0x07) | ((face != 0 && hitY > 0.5f) ? 0x08 : 0x00));
    }

    switch (blockId) {
        // --- Orientados hacia the player (in front = cuadrante) ---
        case block::FURNACE:
        case block::BURNING_FURNACE:
        case block::CHEST:
        case block::DISPENSER:
        case block::DROPPER:
            return BlockOrientation::horizontalFaceMeta(q);

        // --- Pegados to the cara clicked ---
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

        // --- Trampillas: orientación by yaw + bit 0x04 if is monta above ---
        case block::TRAPDOOR:
        case block::TRAPDOOR_IRON:
            return (uint8_t)(BlockOrientation::trapdoorMeta(q) |
                             ((face != 0 && hitY > 0.5f) ? 0x04 : 0x00));

        // --- Yunque: axis this-oeste by default, rota with the yaw ---
        case block::ANVIL:
            return BlockOrientation::anvilMeta(q);

        // --- Calabaza / repeater: miran HACIA the player (opuesto) ---
        case block::PUMPKIN:
        case block::LIT_PUMPKIN:
        case block::UNPOWERED_REPEATER:
        case block::POWERED_REPEATER:
            return (uint8_t)((baseData & 0x0C) | BlockOrientation::yawToMetaOpposite(yaw));

        // --- Troncos / cuarzo pilar / heno: axis according to the cara of click ---
        case block::LOG:
        case 157: /* LOG2 (acacia/dark oak) */
        case block::QUARTZ_BLOCK:
        case block::HAY_BALE:
            return BlockOrientation::axisMeta(face, baseData);

        // Hopper saves its meta (modo of exit); the rest conserva baseData.
        default:
            return baseData;
    }
}

} // namespace

void Player::handleBlockPlace(int x, int y, int z, uint8_t face,
                              int16_t heldItemId, uint8_t heldCount, uint16_t heldDamage,
                              float fx, float fy, float fz) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (!getWorld()) return;

    // ---- Compute placed block position based on face ----
    // This is computed UP FRONT because the flint-and-steel branch below
    // needs placeX/and/Z to call ItemFlintSteel::onActivate(). Previously this
    // calculation lived at the bottom of the function and the flint-and-steel
    // branch referenced placeX/and/Z before they were declared, producing
    // "use of undeclared identifier" compile errors on both GCC and Clang.
    int placeX = x, placeY = y, placeZ = z;
    switch (face) {
        case 0: placeY--; break;  // bottom
        case 1: placeY++; break;  // top
        case 2: placeZ--; break;  // north
        case 3: placeZ++; break;  // south
        case 4: placeX--; break;  // west
        case 5: placeX++; break;  // east
        default: return;
    }
    if (placeY < 0 || placeY >= 256) return;

    auto* lvl = getWorld();

    // Redstone components such as levers and buttons react to to click
    // before normal item/block placement is considered.
    // sneaking not is can accionar a palanca ni a button (vanilla).
    if (!sneaking_ && cursestone::redstone::tryInteract(lvl, srv_, x, y, z)) {
        return;
    }

    // ---- REGLA VANILLA of sneaking -------------------------------
    //
    // while the player this sneaking (PlayerActionPacket
    // ACTION_START_SNEAK=11, ver handlePlayerAction) not is can
    // interactuar with tiles ni with blocks: nothing of abrir cofres, hornos
    // ni mesas, ni accionar palancas/botones, ni chispa, ni cubos, ni
    // disable fire with the mano. it unico permitido is place blocks and
    // ATACAR (the ataque va by handleInteract/INTERACT_PACKET, that not
    // passes by this function, this way that sigue funcionando igual).
    //
    // is the regla estandar of vanilla, that ni Genisys 0.15.0 ni this
    // base of code implementaban: before the sneaking era puramente
    // cosmético in the client of who is agachaba.
    //
    // `sneakLocked` is captures a time to the principio and is consulta in each
    // rama of interaccion of below; the colocacion of blocks not is touches
    // (a puerta, a piston, a sign or any block 1..255 is
    // siguen placing normalmente while is this sneaking).
    const bool sneakLocked = sneaking_;

    // PlayerInteractEvent (RIGHT_CLICK_BLOCK) — is dispara before
    // any rama of logica (flint&steel, extincion of fire,
    // colocacion of block). a listener can cancelarlo (p.ej.
    // proteccion of zona) for bloquear all interaccion with this
    // block without have that duplicar the chequeo in each rama of below.
    // `item` reflects what the client reporta have in mano; ids
    // outside of rango uint16_t (not should pasar, heldItemId is
    // int16_t) is truncan of way safe via the cast explicito.
    //
    // sneaking the evento not is dispara: the interaccion not arrives to
    // ocurrir, this way that anunciarla seria mentirle to the plugins.
    if (!sneakLocked) {
        item::ItemStack heldStack;
        heldStack.id    = (uint16_t)heldItemId;
        heldStack.meta  = (uint8_t)(heldDamage & 0xFF);
        heldStack.count = heldCount;
        auto ev = event::EventManager::get().fire<event::player::PlayerInteractEvent>(
            this, event::player::PlayerInteractEvent::RIGHT_CLICK_BLOCK,
            x, y, z, (int)face, heldStack);
        if (ev.isCancelled()) return;
    }

    // Chest — right-clicking to chest opens its inventory instead of
    // placing to block. This has to sit AFTER the PlayerInteractEvent
    // cancel-check above (so to listener that blocks interaction with
    // this block also blocks opening it) and BEFORE the held-item →
    // placement logic below (to chest should open even if the player is
    // holding to block item — you don't accidentally place to block on
    // top of the chest you're trying to open).
    if (!sneakLocked && lvl->getBlockId(x, y, z) == block::CHEST) {
        openChestAt(x, y, z);
        return;
    }

    // furnace — same pattern that the chest: click derecho opens its UI
    // (its own window of contenedor) instead of place a block above.
    if (!sneakLocked && (lvl->getBlockId(x, y, z) == block::FURNACE ||
                         lvl->getBlockId(x, y, z) == block::BURNING_FURNACE)) {
        openFurnaceAt(x, y, z);
        return;
    }

    // Mesa of crafting (WORKBENCH 58) — the client 0.15.x opens its UI of
    // crafting 3x3 by its cuenta to the click derecho (use-item on block);
    // server-side only garantizamos that it arrives the CraftingDataPacket
    // with the recetas (is takes precedence a time in doFirstSpawn). not is opens
    // none window server-side: the recetas viajan all in a only
    // packet global and the client filtra big/small according to where is
    // crafteando.
    if (!sneakLocked && lvl->getBlockId(x, y, z) == block::WORKBENCH &&
        srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[CRAFT] " + cfg_.username +
            " opened crafting table at " +
            std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
    }

    // ---- Decide what block (if any) to place based on held item ----
    // PMMP-style item→block mapping. item ids 256+ are tools / buckets /
    // etc. and need special handling. item ids 1..255 are direct block
    // placements.
    //
    // NOTE: heldItemId is int16_t (not uint8_t) so that item ids > 255
    // (e.g. 259=flint&steel, 325=bucket, 326=water bucket, 327=lava bucket)
    // survive intact. Earlier the parameter was uint8_t which silently
    // truncated them and made the comparisons below tautologically false.
    uint8_t placedBlockId   = 0;     // 0 = not placement
    uint8_t placedBlockData = 0;
    bool    consumeItem     = false; // decrement inventory stack?
    bool    skipPlaceSound  = false; // for fire/water/lava (PMMP doesn't play it)
    (void)heldCount;  // currently unused — kept for API parity with PMMP
    (void)fx; (void)fz;  // hit point X/Z — the orientación vanilla only uses fy (slabs/trapdoors)

    if (heldItemId == 0) {
        // Empty hand — not placement. BUT, in MCPE 0.15.x the client also
        // sends USE_ITEM_PACKET with an empty hand when the player
        // right-clicks on to FIRE block to extinguish it (PMMP handles this
        // in World::useItemOn → Fire::onUpdate(BLOCK_UPDATE_NORMAL)).
        //
        // We replicate that behaviour here: if the clicked target (x,and,z)
        // is to FIRE block, we set it to AIR and broadcast the update.
        // This makes fire removable with to single right-click without
        // needing to break the block underneath it.
        constexpr uint8_t FIRE_ID = 51;
        uint8_t targetId = lvl->getBlockId(x, y, z);
        if (!sneakLocked && targetId == FIRE_ID) {
            // Get the chunk for (x, and, z) to set the block.
            int32_t tcx = x >> 4, tcz = z >> 4;
            if (mcp_chunk_t* tch = lvl->chunks().getChunkIfLoaded(tcx, tcz)) {
                int tlx = x & 15, tlz = z & 15;
                mcp_chunk_set_block_full(tch, tlx, y, tlz, 0, 0);
                tch->dirty = true;
                cursestone::redstone::notifyNeighbors(lvl, srv_, x, y, z);

                // Broadcast UpdateBlockPacket to nearby players.
                auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
                ubp->x = x;
                ubp->z = z;
                ubp->y = (uint8_t)y;
                ubp->blockId = 0;
                ubp->blockData = 0;
                ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
                if (srv_) {
                    for (auto& other : srv_->playersSnapshot()) {
                        if (!other) continue;
                        if (other->getWorld() != getWorld()) continue;   // same world
                        if (!other->canSeeBlock(tcx, tcz)) continue;
                        other->sendPacket(ubp);
                    }
                }
            }
            return;
        }
        // Not fire — just log "not placement" and bail.
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[BLOCK] " + cfg_.username +
                " attempted place with empty hand — ignored");
        }
        return;
    } else if (heldItemId == 193 || heldItemId == 194 || heldItemId == 195 ||
               heldItemId == 196 || heldItemId == 197 ||
               heldItemId == 324 || heldItemId == 330) {
        // Puerta (any especie of madera, or hierro). is which is
        // the item exacto (193-197 = specie-doors of MCPE 0.15, each a
        // with id of block own in the rango <256; 324 = puerta of
        // madera genérica; 330 = puerta of hierro), the block placed
        // in the world colapsa always to WOODEN_DOOR (64) or IRON_DOOR
        // (71) — ver nota extensa in RedstoneManager.cpp about by what
        // BlockIds.h (that gobierna the World actual) not distingue especies
        // of madera for puertas, unlike ItemConstants.h that yes
        // the distingue as item. Consecuencia visual known and
        // documentada: all puerta of madera is ve as the textura of
        // roble in the client, without importar the item usado for
        // colocarla — the redstone and the toggle work igual for
        // all, that is what importa for this fase of the proyecto.
        //
        // a puerta ocupa 2 blocks verticales, this way that not can
        // seguir the flujo genérico of "a only setBlock" of more below
        // — is colocan ambas mitades here and is returns, just like already
        // does flint&steel some líneas above.
        uint8_t doorId = (heldItemId == 330) ? item::IRON_DOOR_BLOCK
                                              : item::DOOR_BLOCK;
        // Nota: item::DOOR_BLOCK (64) e item::IRON_DOOR_BLOCK (71) are
        // the same valores numéricos that block::WOODEN_DOOR and
        // block::IRON_DOOR in BlockIds.h (confirmado) — coherente with
        // that in MCPE 0.15 item id == block id for items of block.
        // is use the constantes of item:: here because already estaba
        // included that header; not needed include block/BlockIds.h
        // only by esto.

        // not is can place a puerta if not there is espacio for the two
        // mitades, or if the block below of the half inferior not is
        // solid (none verificación of "ground solid" exists hoy in
        // the proyecto for ítems in general — ver the comprobación of
        // "existing != 0" more below, that is the only saves of this
        // tipo that already exists — this way that replicamos exactly that same
        // nivel of rigor: only is requires air in the two celdas target,
        // without exigir soporte solid below, for not introducir a regla
        // more estricta that the rest of the sistema of colocación).
        if (placeY + 1 >= 256) return; // the half upper is saldría of the world

        int32_t cxLower = placeX >> 4, czLower = placeZ >> 4;
        mcp_chunk_t* chLower = lvl->chunks().getChunkIfLoaded(cxLower, czLower);
        if (!chLower) return; // same criterio that the rest: not generate chunk síncronamente
        int localXLower = placeX & 15, localZLower = placeZ & 15;
        uint8_t existingLower = mcp_chunk_get_block_id(chLower, localXLower, placeY, localZLower);
        if (existingLower != 0) return; // cell inferior ocupada

        // the half upper can fall in a chunk different only if
        // placeY+1 cruza to other chunk verticalmente — in this format
        // the chunks are columns complete (0-255 of alto), this way that
        // cxLower/czLower siguen being válidos for and+1 also; not
        // needed volver to resolver the chunk.
        uint8_t existingUpper = mcp_chunk_get_block_id(chLower, localXLower, placeY + 1, localZLower);
        if (existingUpper != 0) return; // cell upper ocupada

        // Orientación by hacia where mira the player when placing,
        // just like vanilla actual. Conversión yaw→4 direcciones
        // cardinales (fórmula estándar: divide the círculo in 4
        // cuadrantes of 90°, centrados in each dirección — the same
        // that uses pistonDirOffset indirectamente vía notifyNeighbors,
        // this way that ambos sistemas remain alineados). Mapeo elegido for
        // this proyecto (esquema own — ver nota in
        // RedstoneManager.cpp about by what not is replica the legado
        // ambiguo of Java): 0=sur, 1=oeste, 2=norte, 3=this.
        // Orientación vanilla Nukkit (BlockDoor.place): {1,2,3,0} about
        // the cuadrante yaw→{S,W,N,E} — tabla only in BlockOrientation.h
        // (reemplaza the fórmula emits that había here).
        uint8_t doorFacing = block::BlockOrientation::doorMeta(
            block::BlockOrientation::quadrant(yaw()));

        uint8_t lowerData = doorFacing & 0x03;       // bits 0-1: orientación; bit 0x04 (abierta) starts in 0; bit 0x08 (upper) in 0
        uint8_t upperData = 0x08 | (doorFacing & 0x03); // bit 0x08 fixed (is upper); conserva orientación so that ambas mitades sepan hacia where mira the puerta without have that consultar the other

        mcp_chunk_set_block_full(chLower, localXLower, placeY, localZLower, doorId, lowerData);
        mcp_chunk_set_block_full(chLower, localXLower, placeY + 1, localZLower, doorId, upperData);
        chLower->dirty = true;

        broadcastBlockChange(srv_, lvl, placeX, placeY, placeZ, doorId, lowerData);
        broadcastBlockChange(srv_, lvl, placeX, placeY + 1, placeZ, doorId, upperData);

        cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY, placeZ);
        cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY + 1, placeZ);

        if (!(cfg_.gamemode & 1) && inv_) { // survival only
            int held = inv_->heldSlot();
            auto cur = inv_->getItem(held);
            if (!cur.isEmpty()) {
                if (cur.count > 1) { cur.count -= 1; inv_->setItem(held, cur); }
                else { inv_->clear(held); }
            }
        }
        return;
    } else if (heldItemId == 33 || heldItemId == item::STICKY_PISTON) {
        // Pistón normal (33) or pegajoso (item::STICKY_PISTON = 29). the
        // pistón normal not has constante with name in
        // ItemConstants.h — is a laguna actual of that file (never is
        // arrived to necesitar before this sesión), this way that is uses the
        // literal numérico just like the rest of this switch already does
        // for other cases especiales (259, 326, 327, 331). the valor 33
        // is confirmado contra BlockIds.h::PISTON_ID.
        //
        // a only block, but needs calcular the dirección by
        // yaw+pitch (unlike the puerta, the pistón yes can
        // apuntar above/below, not only to the 4 direcciones
        // horizontales — just like vanilla actual: mirar claramente hacia
        // above or below when placing it orienta verticalmente).
        uint8_t pistonId = (heldItemId == item::STICKY_PISTON) ? block::STICKY_PISTON : block::PISTON_ID;

        int32_t cxP = placeX >> 4, czP = placeZ >> 4;
        mcp_chunk_t* chP = lvl->chunks().getChunkIfLoaded(cxP, czP);
        if (!chP) return;
        int localXP = placeX & 15, localZP = placeZ & 15;
        uint8_t existingP = mcp_chunk_get_block_id(chP, localXP, placeY, localZP);
        if (existingP != 0) return;

        float pitchDeg = pitch();
        uint8_t pistonFacing;
        if (pitchDeg <= -60.0f) {
            pistonFacing = 1; // mirando marcadamente hacia above → pistón apunta above
        } else if (pitchDeg >= 60.0f) {
            pistonFacing = 0; // mirando marcadamente hacia below → pistón apunta below
        } else {
            // Tabla vanilla Nukkit (pistón): {3,4,2,5} about the same
            // cuadrante yaw→{S,W,N,E} — BlockOrientation.h (before era a
            // tabla local duplicada with the same valores).
            pistonFacing = block::BlockOrientation::pistonMeta(
                block::BlockOrientation::quadrant(yaw()));
        }

        mcp_chunk_set_block_full(chP, localXP, placeY, localZP, pistonId, pistonFacing);
        chP->dirty = true;
        broadcastBlockChange(srv_, lvl, placeX, placeY, placeZ, pistonId, pistonFacing);
        cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY, placeZ);

        if (!(cfg_.gamemode & 1) && inv_) {
            int held = inv_->heldSlot();
            auto cur = inv_->getItem(held);
            if (!cur.isEmpty()) {
                if (cur.count > 1) { cur.count -= 1; inv_->setItem(held, cur); }
                else { inv_->clear(held); }
            }
        }
        return;
    } else if (heldItemId == item::SIGN) {
        // sign (item 323). the block actual is SIGN_POST (63) or WALL_SIGN
        // (68) — none is igual to the id of item, this way that not entra in the
        // rama genérica of "block item" (1..255). Portado of Genisys
        // block/SignPost.php::place():
        //   face 0 (cara of below) → not is can place;
        //   face 1 (cara of above) → sign of pie, meta = rotación yaw;
        //   faces 2..5 (lateral)    → sign of wall, meta = cara clicked.
        if (face == 0) return;
        if (face == 1) {
            placedBlockId   = block::SIGN_POST;   // 63
            placedBlockData = (uint8_t)(
                ((int)std::floor((yaw() + 180.0f) * 16.0f / 360.0f + 0.5f)) & 0x0F);
        } else {
            placedBlockId   = block::WALL_SIGN;   // 68
            // BlockSignPost.place uses meta = cara clicked (2..5), igual
            // that the escalera of mano — ladderMeta it returns tal which.
            placedBlockData = block::BlockOrientation::ladderMeta(face);
        }
        consumeItem = !(cfg_.gamemode & 1);  // survival only
    } else if (heldItemId >= 1 && heldItemId <= 255) {
        // Block item — place it directly. Base data = low 4 bits of
        // heldDamage; above is applies the orientación vanilla
        // (computeVanillaPlacementMeta: escaleras hacia the player,
        // furnace/chest/dispenser of cara, troncos by axis, etc.).
        placedBlockId   = static_cast<uint8_t>(heldItemId);
        placedBlockData = computeVanillaPlacementMeta(
            (uint8_t)heldItemId, (uint8_t)(heldDamage & 0x0F), face, yaw(), fy);
        consumeItem     = !(cfg_.gamemode & 1);  // survival only
    } else if (heldItemId == 331) {
        // Redstone dust item -> Redstone Wire block (55).
        placedBlockId   = 55;
        placedBlockData = 0;
        consumeItem     = !(cfg_.gamemode & 1);
    } else if (heldItemId == 325) {
        // Bucket (empty) — not placement.
        return;
    } else if (sneakLocked && heldItemId > 255) {
        // REGLA VANILLA of sneaking: any item of id > 255
        // (chispa and ferro = 259, cubo = 325, cubo of agua = 326, cubo of
        // lava = 327) is a use of item, not a colocacion of block, and
        // sneaking this prohibido. if not it sacamos aqui caeria in the
        // rama end of "other item — not placement", that is the same
        // result but with a log much less util.
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[SNEAK] " + cfg_.username +
                " tried to use item id=" + std::to_string(heldItemId) +
                " while crouching — blocked (vanilla)");
        }
        return;
    } else if (heldItemId == 326) {
        // Water bucket → place FLOWING_WATER (id=8) so the client renders
        // it as water that spreads. Using WATER (id=9) causes the client
        // to immediately replace it with to "floating lever" placeholder
        // because id=9 is to source-only block that needs adjacent flow.
        placedBlockId  = 8;
        placedBlockData = 0;
        consumeItem     = !(cfg_.gamemode & 1);
        skipPlaceSound  = true;
    } else if (heldItemId == 327) {
        // Lava bucket → place FLOWING_LAVA (id=10).
        placedBlockId  = 10;
        placedBlockData = 0;
        consumeItem     = !(cfg_.gamemode & 1);
        skipPlaceSound  = true;
    } else if (heldItemId == 259 && !sneakLocked) {
        // Flint and steel — delegate all the logic to ItemFlintSteel::onActivate().
        // Sneaking is handled by the sneakLocked branch above, which returns
        // before reaching this point.
        // Handles: solid-block check, Nether portal frame detection and
        // lighting (Infinity/PMMP algorithm), fire placement, first fire
        // tick scheduling and durability.
        item::ItemFlintSteel::onActivate(lvl, this,
            placeX, placeY, placeZ,  // air cell in front of the clicked face
            x, y, z,                 // clicked block (the actual target)
            face);
        return;  // onActivate() already did the setBlock + broadcast
    } else {
        // Other item (food, tool, etc.) — PMMP would call onItemUse; we not-op.
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[BLOCK] " + cfg_.username +
                " used non-block item id=" + std::to_string(heldItemId) + " — no placement");
        }
        return;
    }

    // ---- Place the block at (placeX, placeY, placeZ) ----
    int32_t cx = placeX >> 4;
    int32_t cz = placeZ >> 4;
    // CRITICAL: use getChunkIfLoaded, NOT getChunk. The latter triggers
    // synchronous chunk generation on the main thread when the player
    // right-clicks into an unloaded chunk, which can stall the tick loop
    // for hundreds of milliseconds (chunk gen + populate). PMMP loads
    // chunks via an async worker pool; we don't have that yet, so for
    // now we just not-op the placement if the chunk isn't resident.
    // The player will see "block didn't place" and can retry; that's
    // strictly better than freezing the whole server.
    mcp_chunk_t* ch = lvl->chunks().getChunkIfLoaded(cx, cz);
    if (!ch) {
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[BLOCK] " + cfg_.username +
                " tried to place block in unloaded chunk (" +
                std::to_string(cx) + "," + std::to_string(cz) + ") — ignored");
        }
        return;
    }

    int localX = placeX & 15;
    int localZ = placeZ & 15;

    // Don't place if target is not air, UNLESS the existing block is FIRE.
    // FIRE is canBeReplaced=true per PMMP, so placing to block on top of
    // to fire should extinguish the fire and put the new block in its place.
    // Without this, the client would locally replace the fire with the new
    // block (because canBeReplaced=true) but the server would keep the fire
    // in its chunk → "ghost fire" that damages entities passing through
    // even though visually it's gone. And breaking the new block would
    // then drop to fire item (because the server still has FIRE there).
    uint8_t existing = mcp_chunk_get_block_id(ch, localX, placeY, localZ);
    constexpr uint8_t FIRE_ID = 51;
    if (existing != 0 && existing != FIRE_ID) return;

    // BlockPlaceEvent — a listener can cancelarlo (p.ej. protección
    // of zonas) for impedir the colocación without touch inventory ni chunk.
    {
        auto ev = event::EventManager::get().fire<event::block::BlockPlaceEvent>(
            this, math::Vector3{(double)placeX, (double)placeY, (double)placeZ},
            (int)placedBlockId);
        if (ev.isCancelled()) return;
    }

    // Set the block in the chunk.
    mcp_chunk_set_block_full(ch, localX, placeY, localZ, placedBlockId, placedBlockData);
    ch->dirty = true;

    // Block-entity: the carteles necesitan a TileSign in the registro of the
    // World so that the text (and the own tile) sobreviva to the save/reload.
    // PMMP creates the tile when placing the block; acá is does explícito. Esto
    // is what arregla the "sign that disappears to the re-ingresar": before
    // the ítem 323 not colocaba nothing server-side (ni block ni tile).
    if (placedBlockId == block::SIGN_POST || placedBlockId == block::WALL_SIGN) {
        auto sign = std::make_shared<tile::TileSign>(
            cursestone::world::Position((double)placeX, (double)placeY, (double)placeZ));
        sign->setCreator(cfg_.uuid);
        lvl->setTile(placeX, placeY, placeZ, sign);
    }

    // Redstone: placing to component may connect or power to neighboring circuit.
    cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY, placeZ);

    // Decrement inventory (only in survival — PMMP skips in creative).
    if (consumeItem && inv_) {
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

    // Broadcast UpdateBlockPacket.
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = placeX;
    ubp->z = placeZ;
    ubp->y = (uint8_t)placeY;
    ubp->blockId = placedBlockId;
    ubp->blockData = placedBlockData;
    ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;

    // PMMP sends to place sound via LevelEventPacket with
    // evid = EVENT_SOUND_BLOCK_PLACE (1052).
    //   - Position: BLOCK CORNER (not +0.5).
    //   - Data:     just the placed block id (not meta bits).
    auto sndEv = std::make_shared<network::protocol::LevelEventPacket>();
    sndEv->evid = network::protocol::LevelEventPacket::EVENT_SOUND_BLOCK_PLACE;
    sndEv->x = (float)placeX;
    sndEv->y = (float)placeY;
    sndEv->z = (float)placeZ;
    sndEv->data = (int32_t)placedBlockId;

    // Same visibility filter as for block break (PMMP `Level::addSound` /
    // `getChunkPlayers`) — distance based, see canSeeBlock().
    int32_t pk_cx = placeX >> 4;
    int32_t pk_cz = placeZ >> 4;
    int placeInView = 0, placeSent = 0;
    if (srv_) {
        for (auto& other : srv_->playersSnapshot()) {
            if (!other) continue;
            if (other->getWorld() != getWorld()) continue;   // same world
            if (!other->canSeeBlock(pk_cx, pk_cz)) continue;
            ++placeInView;
            if (!other->spawned()) continue;
            other->sendPacket(ubp);
            // Skip the place sound for fire/water/lava (PMMP behaviour).
            if (!skipPlaceSound) other->sendPacket(sndEv);
            ++placeSent;
        }
    }

    // the sign recién placed: sincroniza its BlockEntityData to those that
    // trackean the chunk (PMMP Sign::spawnToAll() to the crearse the tile), already
    // with the block difundido, so that the client tenga the entity.
    if (placedBlockId == block::SIGN_POST || placedBlockId == block::WALL_SIGN) {
        auto t = lvl->getTile(placeX, placeY, placeZ);
        if (t) sendBlockEntityData(placeX, placeY, placeZ, *t, nullptr);
    }

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[BLOCK] " + cfg_.username + " placed block at " +
            std::to_string(placeX) + "," + std::to_string(placeY) + "," + std::to_string(placeZ) +
            " (id=" + std::to_string(placedBlockId) + " data=" + std::to_string(placedBlockData) +
            " fromItem=" + std::to_string(heldItemId) + ")");
        // diagnostic of the bug "place/remove blocks not is ve in
        // multiplayer" — ver the comment equivalente in handleBlockBreak.
        utils::MainLogger::get().info("[BLOCK-SYNC] place at chunk " +
            std::to_string(pk_cx) + "," + std::to_string(pk_cz) +
            " -> in_view=" + std::to_string(placeInView) +
            " sent=" + std::to_string(placeSent));
    }
}

// ============================================================
// Block drop lookup table — mirrors PMMP `Block::getDrops(item $item)`
// per-block overrides. Returns {itemId, damage}. {0, 0} = not drop.
// Default (unlisted block): drop itself (id, damage, count=1).
// ============================================================
static std::pair<uint16_t, uint16_t> lookupBlockDrop(uint8_t blockId, uint8_t blockData) {
    switch (blockId) {
        case 1:  return {4,   blockData & 0x07};   // Stone -> Cobblestone
        case 2:  return {3,   0};                   // Grass -> Dirt
        case 15: return {0,   0};                   // Iron Ore -> not drop (smelt)
        case 16: return {263, 0};                   // Coal Ore -> Coal
        case 14: return {0,   0};                   // Gold Ore -> not drop (smelt)
        case 21: return {351, 4};                   // Lapis Ore -> Lapis Lazuli dye
        case 56: return {264, 0};                   // Diamond Ore -> Diamond
        case 73: return {331, 0};                   // Redstone Ore -> Redstone
        case 55: return {331, 0};                   // Redstone Wire -> Redstone dust
        case 76: return {75,  0};                   // Redstone Torch (ON) -> OFF item
        case 124: return {123, 0};                  // Redstone Lamp (ON) -> OFF item
        case 20: return {0,   0};                   // Glass -> not drop
        case 79: return {0,   0};                   // Ice -> not drop
        case 89: return {348, 0};                   // Glowstone -> Glowstone Dust
        case 18: return {0,   0};                   // Leaves -> not drop (chance-based)
        case 7:  return {0,   0};                   // Bedrock -> unbreakable
        case 46: return {0,   0};                   // TNT -> not drop
        case 51: return {0,   0};                   // FIRE -> not drop (PMMP Fire::getDrops() = [])
        case 54: return {54,  0};                   // Chest -> chest item
        case 61: return {61,  0};                   // furnace -> furnace item
        case 62: return {61,  0};                   // Burning furnace -> furnace item
        case 63: return {323, 0};                   // Standing Sign -> sign item (Genisys SignPost::getDrops)
        case 68: return {323, 0};                   // Wall Sign -> sign item (Genisys WallSign::getDrops)
        default: return {blockId, blockData};       // Default: drop self
    }
}

bool Player::canInteract(math::Vector3 pos, double maxDistance) const {
    double dx = pos.x - pos_.x;
    double dy = pos.y - pos_.y;
    double dz = pos.z - pos_.z;
    return (dx*dx + dy*dy + dz*dz) <= maxDistance * maxDistance;
}
std::vector<item::ItemStack> Player::getDrops() const {
    std::vector<item::ItemStack> drops;
    if (inv_) {
        for (int i = 0; i < inventory::PlayerInventory::SIZE; ++i) {
            auto it = inv_->getItem(i);
            if (!it.isEmpty()) drops.push_back(it);
        }
    }
    return drops;
}} // namespace cursestone
