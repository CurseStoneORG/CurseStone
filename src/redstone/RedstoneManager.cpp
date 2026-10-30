/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
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
#include "RedstoneManager.h"
#include "block/BlockIds.h"
#include "world/World.h"
#include "Server.h"
#include "Player.h"
#include "network/protocol/UpdateBlockPacket.h"
#include "network/protocol/LevelEventPacket.h"
#include "event/EventManager.h"
#include "event/block/DoorToggleEvent.h"
#include <algorithm>

namespace cursestone::redstone {

using cursestone::world::World;

// broadcastBlock — push a block update to every player viewing chunk (x,z).
static void broadcastBlock(cursestone::Server* server, int32_t x, int32_t y, int32_t z,
                           uint8_t blockId, uint8_t blockData) {
    if (!server)
        return;
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = x;
    ubp->z = z;
    ubp->y = (uint8_t)y;
    ubp->blockId = blockId;
    ubp->blockData = blockData;
    ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;

    int32_t cx = x >> 4;
    int32_t cz = z >> 4;
    for (auto& player : server->playersSnapshot()) {
        if (!player)
            continue;
        if (!player->canSeeBlock(cx, cz))
            continue;
        player->sendPacket(ubp);
    }
}

// broadcastLevelEvent — same audience as broadcastBlock() but for a
// LevelEventPacket (sounds / particles / weather). Position semantics
// follow the reference server Level::addSound(): the packet carries the coordinates
// AS GIVEN (block corner for sounds; callers pass what the reference
// passes). Used for the door open/close sound (EVENT_SOUND_DOOR) on both
// the hand-toggle and the redstone-toggle paths.
static void broadcastLevelEvent(cursestone::Server* server, int16_t evid, int32_t data, float x,
                                float y, float z) {
    if (!server)
        return;
    auto pk = std::make_shared<network::protocol::LevelEventPacket>();
    pk->evid = evid;
    pk->data = data;
    pk->x = x;
    pk->y = y;
    pk->z = z;
    int32_t cx = (int32_t)std::floor(x) >> 4;
    int32_t cz = (int32_t)std::floor(z) >> 4;
    for (auto& player : server->playersSnapshot()) {
        if (!player)
            continue;
        if (!player->canSeeBlock(cx, cz))
            continue;
        player->sendPacket(pk);
    }
}

// playDoorSound — the wooden/iron door creak (the reference server
// level/sound/DoorSound → LevelEventPacket evid=1003 at the door
// block position, data = pitch*1000 = 0). The 0.15.10 client needs this
// or the door toggles silently (bug: "las puertas no emiten sonido").
static void playDoorSound(cursestone::Server* server, int32_t x, int32_t y, int32_t z) {
    broadcastLevelEvent(server, network::protocol::LevelEventPacket::EVENT_SOUND_DOOR, 0, (float)x,
                        (float)y, (float)z);
}

// getEmittedPower — signal strength this block feeds outward.
int getEmittedPower(World* world, int32_t x, int32_t y, int32_t z) {
    if (!world)
        return 0;
    if (y < 0 || y >= 256)
        return 0;
    // Never read blocks in unloaded chunks: World::getBlockId would
    // trigger synchronous generation (the BlockFire v5 freeze pattern).
    // An unloaded neighbor counts as "no signal".
    if (!world->isChunkLoaded(x, z))
        return 0;
    uint8_t id = world->getBlockId(x, y, z);
    uint8_t data = world->getBlockData(x, y, z);

    switch (id) {
    case block::REDSTONE_TORCH_ON:
        // Lit torch: full power sideways and up, never into the
        // supporting block below (vanilla behavior).
        return 15;
    case block::REDSTONE_BLOCK:
        return 15;
    case block::LEVER:
        // Bit 0x8 = lit (vanilla lever convention).
        return (data & 0x08) ? 15 : 0;
    case block::STONE_BUTTON:
        return (data & 0x08) ? 15 : 0;
    case block::WOOD_PRESSURE_PLATE:
    case block::STONE_PRESSURE_PLATE:
        // data almacena 0 (sin pisar) o 1 (pisada) para placas simples.
        return (data != 0) ? 15 : 0;
    case block::REDSTONE_WIRE:
        // El wire emite su propia potencia actual (almacenada en data).
        return data & 0x0F;
    default:
        return 0;
    }
}

// isPowered — does this block receive signal from a neighbor? Not the
// inverse of getEmittedPower: an off lever emits nothing, but a block
// NEXT TO a lit lever is powered.
bool isPowered(World* world, int32_t x, int32_t y, int32_t z) {
    if (!world)
        return false;
    // Los 6 vecinos directos.
    static const int32_t dx[6] = {1, -1, 0, 0, 0, 0};
    static const int32_t dy[6] = {0, 0, 1, -1, 0, 0};
    static const int32_t dz[6] = {0, 0, 0, 0, 1, -1};
    for (int i = 0; i < 6; ++i) {
        int32_t nx = x + dx[i], ny = y + dy[i], nz = z + dz[i];
        if (ny < 0 || ny >= 256)
            continue;
        if (!world->isChunkLoaded(nx, nz))
            continue;
        if (getEmittedPower(world, nx, ny, nz) > 0)
            return true;
    }
    // A wire with power > 0 counts as powered itself (blocks on top
    // of it query this).
    if (!world->isChunkLoaded(x, z))
        return false;
    uint8_t id = world->getBlockId(x, y, z);
    if (id == block::REDSTONE_WIRE) {
        return (world->getBlockData(x, y, z) & 0x0F) > 0;
    }
    return false;
}

// tryInteract — lever and button. Vanilla layout: bits 0-2 (or 0-5 by
// variant) hold the placement face, bit 0x08 the on/off state. Toggling
// keeps the other bits intact so the visual orientation never changes.
bool tryInteract(World* world, cursestone::Server* server, int32_t x, int32_t y, int32_t z,
                 cursestone::Player* player) {
    if (!world)
        return false;
    if (y < 0 || y >= 256)
        return false;
    if (!world->isChunkLoaded(x, z))
        return false;
    uint8_t id = world->getBlockId(x, y, z);
    uint8_t data = world->getBlockData(x, y, z);

    if (id == block::LEVER || id == block::STONE_BUTTON) {
        // A vanilla button auto-releases after a moment; that needs the
        // scheduler (future work). Both act as manual toggles for now.
        uint8_t newData = data ^ 0x08;
        world->setBlock(x, y, z, id, newData);
        broadcastBlock(server, x, y, z, id, newData); // FIX: was only visible after reconnecting
        notifyNeighbors(world, server, x, y, z);
        return true;
    }

    if (id == block::WOODEN_DOOR) {
        // Wooden doors open on click as well as redstone; iron doors
        // deliberately never do (vanilla).
        //
        // Only the LOWER half stores the 0x04 open bit (see updateDoorAt);
        // clicks may arrive against either half, so resolve the lower one
        // before toggling.
        bool isTopHalf = (data & 0x08) != 0;
        int32_t lowerY = isTopHalf ? (y - 1) : y;
        if (lowerY < 0 || lowerY >= 256)
            return false;
        if (!world->isChunkLoaded(x, z))
            return false;
        uint8_t lowerId = world->getBlockId(x, lowerY, z);
        if (lowerId != block::WOODEN_DOOR)
            return false; // desincronizada — no tocar
        uint8_t lowerData = world->getBlockData(x, lowerY, z);
        if ((lowerData & 0x08) != 0)
            return false; // salvaguarda de datos corruptos, igual que updateDoorAt

        // DoorToggleEvent (Nukkit/the reference server block/Door onActivate fires
        // DoorSound; the event type itself is this project's port of
        // cn.nukkit.event.block.DoorToggleEvent). Cancellable: a
        // protected-door plugin blocks the toggle and the click is
        // consumed (no accidental block placement INTO the door cell).
        {
            auto ev = event::EventManager::get().fire<event::block::DoorToggleEvent>(x, lowerY, z,
                                                                                     player);
            if (ev.isCancelled())
                return true;
        }

        uint8_t newLowerData = lowerData ^ 0x04;
        world->setBlock(x, lowerY, z, block::WOODEN_DOOR, newLowerData);
        broadcastBlock(server, x, lowerY, z, block::WOODEN_DOOR, newLowerData);
        // FIX ("las puertas no emiten sonido"): the 0.15.10 client only
        // animates the swing locally — the creak itself is a server-side
        // LevelEventPacket (the reference server DoorSound), played for every viewer
        // including the clicker.
        playDoorSound(server, x, lowerY, z);
        // No notifyNeighbors here: a manual toggle must not immediately
        // re-evaluate against active signal (the click would undo itself).
        // The next real redstone event recalculates normally.
        return true;
    }
    return false;
}

// updateWireAt — recompute one wire and propagate to neighbor wires.
// Strictly bounded: signal drops 1 per step over 15 levels, so the
// recursion can never run past 15 consecutive wire blocks.
static void updateWireInternal(World* world, cursestone::Server* server, int32_t x, int32_t y,
                               int32_t z, int depth) {
    if (!world)
        return;
    if (depth > 16)
        return; // extra safety guard (never reached)
    if (y < 0 || y >= 256)
        return;
    if (!world->isChunkLoaded(x, z))
        return;
    uint8_t id = world->getBlockId(x, y, z);
    if (id != block::REDSTONE_WIRE)
        return;

    uint8_t oldData = world->getBlockData(x, y, z);
    int oldPower = oldData & 0x0F;

    // Wire power = max of:
    //  - the strongest direct non-wire neighbor signal (torch, redstone
    //    block, lever...), undecayed
    //  - the strongest neighbor wire power minus 1
    int best = 0;
    static const int32_t dx[6] = {1, -1, 0, 0, 0, 0};
    static const int32_t dy[6] = {0, 0, 1, -1, 0, 0};
    static const int32_t dz[6] = {0, 0, 0, 0, 1, -1};
    for (int i = 0; i < 6; ++i) {
        int32_t nx = x + dx[i], ny = y + dy[i], nz = z + dz[i];
        if (ny < 0 || ny >= 256)
            continue;
        if (!world->isChunkLoaded(nx, nz))
            continue;
        uint8_t nid = world->getBlockId(nx, ny, nz);
        if (nid == block::REDSTONE_WIRE) {
            int npower = world->getBlockData(nx, ny, nz) & 0x0F;
            best = std::max(best, npower - 1);
        } else {
            int emitted = getEmittedPower(world, nx, ny, nz);
            best = std::max(best, emitted);
        }
    }
    best = std::max(0, std::min(15, best));

    if (best == oldPower)
        return; // unchanged — stop propagating

    world->setBlock(x, y, z, block::REDSTONE_WIRE, (uint8_t)best);
    // FIX: without this broadcast the wire update was only visible
    // after reconnecting.
    broadcastBlock(server, x, y, z, block::REDSTONE_WIRE, (uint8_t)best);

    // Non-wire consumers fed by this wire (lamps, torches via their
    // support, doors, pistons) must re-evaluate too. Keep this list in
    // sync with notifyNeighbors(): the wire cascade is the only path a
    // signal change reaches a block that is not adjacent to the source.
    // Without it, lever→wire→piston circuits never fire.
    for (int i = 0; i < 6; ++i) {
        int32_t nx = x + dx[i], ny = y + dy[i], nz = z + dz[i];
        if (ny < 0 || ny >= 256)
            continue;
        if (!world->isChunkLoaded(nx, nz))
            continue;
        uint8_t nid = world->getBlockId(nx, ny, nz);
        if (nid == block::REDSTONE_WIRE) {
            updateWireInternal(world, server, nx, ny, nz, depth + 1);
        } else if (nid == block::REDSTONE_LAMP || nid == block::LIT_REDSTONE_LAMP) {
            updateLampAt(world, server, nx, ny, nz);
        } else if (nid == block::WOODEN_DOOR || nid == block::IRON_DOOR) {
            updateDoorAt(world, server, nx, ny, nz);
        } else if (nid == block::PISTON_ID || nid == block::STICKY_PISTON) {
            updatePistonAt(world, server, nx, ny, nz);
        } else if ((nid == block::REDSTONE_TORCH_ON || nid == block::REDSTONE_TORCH_OFF) &&
                   nx == x && nz == z && ny == y + 1) {
            // Same rule as notifyNeighbors: only if this wire IS the
            // torch's support (directly below it).
            updateRedstoneTorchAt(world, server, nx, ny, nz);
        }
    }
}

void updateWireAt(World* world, cursestone::Server* server, int32_t x, int32_t y, int32_t z) {
    updateWireInternal(world, server, x, y, z, 0);
}

// updateLampAt — the redstone lamp is solid+dark (123) unpowered and
// solid+lit (124) powered. It only consumes signal, so no cascade.
void updateLampAt(World* world, cursestone::Server* server, int32_t x, int32_t y, int32_t z) {
    if (!world)
        return;
    if (y < 0 || y >= 256)
        return;
    if (!world->isChunkLoaded(x, z))
        return;
    uint8_t id = world->getBlockId(x, y, z);
    if (id != block::REDSTONE_LAMP && id != block::LIT_REDSTONE_LAMP)
        return;

    bool powered = isPowered(world, x, y, z);
    uint8_t wantId = powered ? block::LIT_REDSTONE_LAMP : block::REDSTONE_LAMP;
    if (wantId == id)
        return; // already in the right state

    world->setBlock(x, y, z, wantId, 0);
    broadcastBlock(server, x, y, z, wantId, 0);
}

// updateRedstoneTorchAt — the redstone torch is an inverter: off when
// its support block is powered, on otherwise. Only the standing
// orientation is handled (support = block below); wall torches need the
// face bits in `data`, which this engine does not interpret yet. The
// torch does not cascade by itself — its neighbors are revisited by the
// caller (notifyNeighbors).
void updateRedstoneTorchAt(World* world, cursestone::Server* server, int32_t x, int32_t y,
                           int32_t z) {
    if (!world)
        return;
    if (y < 1 || y >= 256)
        return; // necesita soporte debajo; y=0 no tiene bloque debajo
    if (!world->isChunkLoaded(x, z))
        return;
    uint8_t id = world->getBlockId(x, y, z);
    if (id != block::REDSTONE_TORCH_ON && id != block::REDSTONE_TORCH_OFF)
        return;

    // Scope: this function only decides ON vs OFF from signal. Support
    // removal (breaking the block below) would drop the torch as an item
    // in vanilla — no block in this project implements support physics
    // yet; that is a cross-cutting future feature, not redstone-specific.

    // Support = the block directly below (x, y-1, z). Its powered state
    // must exclude the torch itself: isPowered(support) would see the
    // torch's own emission of 15 and the torch could never turn off.
    // So this check is inlined: scan the support's 5 other neighbors
    // (4 sides + below), exactly like isPowered minus the up neighbor.
    int32_t sx = x, sy = y - 1, sz = z;
    if (!world->isChunkLoaded(sx, sz))
        return;
    static const int32_t dx[5] = {1, -1, 0, 0, 0};
    static const int32_t dz[5] = {0, 0, 1, -1, 0};
    static const int32_t dy[5] = {0, 0, 0, 0, -1};
    bool supportPowered = false;
    for (int i = 0; i < 5; ++i) {
        int32_t nx = sx + dx[i], ny = sy + dy[i], nz = sz + dz[i];
        if (ny < 0 || ny >= 256)
            continue;
        if (!world->isChunkLoaded(nx, nz))
            continue;
        if (getEmittedPower(world, nx, ny, nz) > 0) {
            supportPowered = true;
            break;
        }
    }
    // The support itself may be a powered wire (same case isPowered
    // covers for wires).
    if (!supportPowered) {
        uint8_t supportId = world->getBlockId(sx, sy, sz);
        if (supportId == block::REDSTONE_WIRE && (world->getBlockData(sx, sy, sz) & 0x0F) > 0) {
            supportPowered = true;
        }
    }

    uint8_t wantId = supportPowered ? block::REDSTONE_TORCH_OFF : block::REDSTONE_TORCH_ON;
    if (wantId == id)
        return;

    world->setBlock(x, y, z, wantId, 0);
    broadcastBlock(server, x, y, z, wantId, 0);
}

// ============================================================
// updateDoorAt — locates the lower half and decides the new state from
// isPowered(). May be called with either half — notifyNeighbors does
// not need to know which one.
//
// `data` FORMAT OF THIS ENGINE (own scheme, NOT Java's legacy layout —
// Java packs 5 door bits into 4 and the top/bottom flag is ambiguous
// across versions (historical double-door bugs). This server does not
// need Java save compatibility, so it defines an unambiguous scheme):
//   - Bit 0x08: ALWAYS "is the upper half" — present on BOTH halves,
//     never reused for anything else. 0 = lower, 1 = upper. Reading
//     bit 0x08 always tells which half is which.
//   - Bit 0x04: "open/closed" — only meaningful on the LOWER half
//     (where 0x08=0); BlockDoor::isOpen()/toggle() agree on it.
//     Undefined on the upper half.
//   - Bits 0x01-0x02: hinge orientation, set at placement and never
//     touched by this engine.
// ============================================================
void updateDoorAt(World* world, cursestone::Server* server, int32_t x, int32_t y, int32_t z) {
    if (!world)
        return;
    if (y < 0 || y >= 256)
        return;
    if (!world->isChunkLoaded(x, z))
        return;
    uint8_t id = world->getBlockId(x, y, z);
    if (id != block::WOODEN_DOOR && id != block::IRON_DOOR)
        return;

    uint8_t data = world->getBlockData(x, y, z);
    bool isTopHalf = (data & 0x08) != 0;
    int32_t lowerY = isTopHalf ? (y - 1) : y;
    if (lowerY < 0 || lowerY >= 256)
        return;
    if (!world->isChunkLoaded(x, z))
        return;
    uint8_t lowerId = world->getBlockId(x, lowerY, z);
    if (lowerId != id)
        return; // la mitad inferior no coincide (puerta rota a medias / desincronizada) — no tocar nada

    uint8_t lowerData = world->getBlockData(x, lowerY, z);
    if ((lowerData & 0x08) != 0)
        return; // safeguard: "lower" half carries the upper bit — corrupt data, leave it

    bool currentlyOpen = (lowerData & 0x04) != 0;
    bool shouldBeOpen = isPowered(world, x, lowerY, z);
    if (shouldBeOpen == currentlyOpen)
        return; // sin cambio

    uint8_t newLowerData = lowerData ^ 0x04;
    world->setBlock(x, lowerY, z, id, newLowerData);
    broadcastBlock(server, x, lowerY, z, id, newLowerData);
    // FIX ("las puertas no emiten sonido"): redstone also toggles doors,
    // and the creak is a server-side LevelEventPacket on protocol 84 —
    // same call the hand-toggle path in tryInteract() makes.
    playDoorSound(server, x, lowerY, z);

    // La mitad superior de este motor solo guarda el bit 0x08 (es
    // The upper half stores only 0x08 + hinge bits, never the open bit,
    // so it needs no data change on toggle: the MCPE client animates the
    // whole door from the lower half. Leaving it untouched also keeps
    // its hinge bits safe.
}

// ============================================================
// updatePistonAt — extend/retract from isPowered(). Facing bits 0-2 of
// `data` (vanilla): 0=down, 1=up, 2=north(-Z), 3=south(+Z), 4=west(-X),
// 5=east(+X). Bit 0x08 = extended.
// ============================================================
static void pistonDirOffset(uint8_t dirBits, int32_t& ox, int32_t& oy, int32_t& oz) {
    ox = oy = oz = 0;
    switch (dirBits & 0x07) {
    case 0:
        oy = -1;
        break; // abajo
    case 1:
        oy = 1;
        break; // arriba
    case 2:
        oz = -1;
        break; // norte
    case 3:
        oz = 1;
        break; // sur
    case 4:
        ox = -1;
        break; // oeste
    case 5:
        ox = 1;
        break; // este
    default:
        break; // 6/7 are not valid vanilla facings — safe no-op
    }
}

void updatePistonAt(World* world, cursestone::Server* server, int32_t x, int32_t y, int32_t z) {
    if (!world)
        return;
    if (y < 0 || y >= 256)
        return;
    if (!world->isChunkLoaded(x, z))
        return;
    uint8_t id = world->getBlockId(x, y, z);
    if (id != block::PISTON_ID && id != block::STICKY_PISTON)
        return;

    uint8_t data = world->getBlockData(x, y, z);
    bool currentlyExtended = (data & 0x08) != 0;
    bool shouldExtend = isPowered(world, x, y, z);
    if (shouldExtend == currentlyExtended)
        return; // sin cambio

    int32_t ox, oy, oz;
    pistonDirOffset(data, ox, oy, oz);
    int32_t headX = x + ox, headY = y + oy, headZ = z + oz;
    if (headY < 0 || headY >= 256)
        return; // the head would leave the world — safe no-op
    if (!world->isChunkLoaded(headX, headZ))
        return;

    if (shouldExtend) {
        // Push only into pure air. Fluids and other replaceables need
        // per-type rules this engine does not cover yet (documented
        // future work instead of wrong behavior).
        uint8_t destId = world->getBlockId(headX, headY, headZ);
        // Blocked — the piston stays retracted, like vanilla. No pending
        // signal bookkeeping needed: breaking the blocker notifies its
        // neighbors, which includes this piston, so it re-evaluates on
        // its own.
        if (destId != 0)
            return;

        uint8_t newData = data | 0x08;
        world->setBlock(x, y, z, id, newData);
        broadcastBlock(server, x, y, z, id, newData);

        world->setBlock(headX, headY, headZ, block::PISTON_HEAD, data & 0x07);
        broadcastBlock(server, headX, headY, headZ, block::PISTON_HEAD, data & 0x07);
        // No notifyNeighbors on the head: the piston head is a technical
        // block that neither sources nor conducts redstone. A wire behind
        // it waits for the next event touching it (rare in practice,
        // documented rather than widening this function's scope).
    } else {
        uint8_t newData = data & ~0x08;
        world->setBlock(x, y, z, id, newData);
        broadcastBlock(server, x, y, z, id, newData);

        // Retract: the head returns to air. A sticky piston would also
        // pull the pushed block back — not implemented (needs push
        // memory); documented future work.
        uint8_t headId = world->getBlockId(headX, headY, headZ);
        if (headId == block::PISTON_HEAD) {
            world->setBlock(headX, headY, headZ, 0, 0);
            broadcastBlock(server, headX, headY, headZ, 0, 0);
            // Gravity checks live inside World::setBlock — clearing the
            // head re-evaluates the block above (sand/gravel/anvils fall).
        }
    }
}

// ============================================================
// notifyNeighbors — punto de entrada principal. Se llama tras romper o
// colocar un bloque en (x,y,z); revisa los 6 vecinos y dispara su
// update if they are redstone components.
// ============================================================
void notifyNeighbors(World* world, cursestone::Server* server, int32_t x, int32_t y, int32_t z) {
    if (!world)
        return;
    static const int32_t dx[6] = {1, -1, 0, 0, 0, 0};
    static const int32_t dy[6] = {0, 0, 1, -1, 0, 0};
    static const int32_t dz[6] = {0, 0, 0, 0, 1, -1};

    // El propio bloque, si es wire (por ejemplo se acaba de colocar
    // source), lamp, torch, door or piston, also needs recomputing.
    if (y >= 0 && y < 256 && world->isChunkLoaded(x, z)) {
        uint8_t selfId = world->getBlockId(x, y, z);
        if (selfId == block::REDSTONE_WIRE) {
            updateWireAt(world, server, x, y, z);
        } else if (selfId == block::REDSTONE_LAMP || selfId == block::LIT_REDSTONE_LAMP) {
            updateLampAt(world, server, x, y, z);
        } else if (selfId == block::REDSTONE_TORCH_ON || selfId == block::REDSTONE_TORCH_OFF) {
            updateRedstoneTorchAt(world, server, x, y, z);
        } else if (selfId == block::WOODEN_DOOR || selfId == block::IRON_DOOR) {
            updateDoorAt(world, server, x, y, z);
        } else if (selfId == block::PISTON_ID || selfId == block::STICKY_PISTON) {
            updatePistonAt(world, server, x, y, z);
        }
    }

    for (int i = 0; i < 6; ++i) {
        int32_t nx = x + dx[i], ny = y + dy[i], nz = z + dz[i];
        if (ny < 0 || ny >= 256)
            continue;
        if (!world->isChunkLoaded(nx, nz))
            continue;
        uint8_t nid = world->getBlockId(nx, ny, nz);
        if (nid == block::REDSTONE_WIRE) {
            updateWireAt(world, server, nx, ny, nz);
        } else if (nid == block::REDSTONE_LAMP || nid == block::LIT_REDSTONE_LAMP) {
            updateLampAt(world, server, nx, ny, nz);
        } else if (nid == block::REDSTONE_TORCH_ON || nid == block::REDSTONE_TORCH_OFF) {
            // La antorcha reacciona a cambios en su SOPORTE (el bloque
            // de abajo), no a cualquier vecino lateral — en vanilla
            // vanilla: a lateral signal change does not affect it. Only
            // recompute when the modified neighbor IS its support
            // es decir, si estamos mirando "hacia arriba" desde (x,y,z)
            // hacia la antorcha (nx,ny,nz) = (x,y+1,z).
            if (nx == x && nz == z && ny == y + 1) {
                updateRedstoneTorchAt(world, server, nx, ny, nz);
            }
        } else if (nid == block::WOODEN_DOOR || nid == block::IRON_DOOR) {
            updateDoorAt(world, server, nx, ny, nz);
        } else if (nid == block::PISTON_ID || nid == block::STICKY_PISTON) {
            updatePistonAt(world, server, nx, ny, nz);
        }
        // Levers, buttons and pressure plates never recompute from
        // neighbor changes — player interaction drives their state.
    }
}

} // namespace cursestone::redstone
