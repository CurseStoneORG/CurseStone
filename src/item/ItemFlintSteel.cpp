// ItemFlintSteel.cpp — Portado de pocketmine/item/FlintSteel.php (PMMP)
// Ver ItemFlintSteel.h para notas de porte.
#include "ItemFlintSteel.h"
#include "ItemStack.h"
#include "block/BlockFire.h"
#include "block/BlockIds.h"
#include "level/Level.h"
#include "Player.h"
#include "inventory/PlayerInventory.h"
#include "network/protocol/UpdateBlockPacket.h"
#include "Server.h"
#include <memory>
#include <algorithm>   // std::min

namespace atomikpe::item {

using namespace atomikpe::block;

// ============================================================
// applyDurability
// mirrors: $this->useOn($block, 2) en Tool.php — suma 2 a meta,
// destruye el ítem si supera MAX_DURABILITY.
// ============================================================
void ItemFlintSteel::applyDurability(Player* player) {
    if (!player || !player->isSurvival()) return;

    auto inv = player->getInventory();
    if (!inv) return;

    auto held = inv->getHeldItem();
    if (held.isEmpty() || held.id != 259) return;

    held.meta += 2;
    if (held.meta > MAX_DURABILITY) {
        // Herramienta rota: quitar del inventario
        inv->clear(inv->heldSlot());
        // TODO: reproducir sonido de ítem roto (LevelEventPacket)
    } else {
        inv->setItemInHand(held);
    }
    player->sendSlot(inv->heldSlot());
}

// ============================================================
// checkPortalFrameX / fillPortalX — eje X
// mirrors: el bloque "x方向" de FlintSteel.php
// ============================================================
bool ItemFlintSteel::checkPortalFrameX(level::Level* level,
                                       int32_t xMin, int32_t xMax,
                                       int32_t yBase, int32_t yMax,
                                       int32_t tz) {
    // Verificar que el techo (yMax) esté completamente cubierto de obsidiana
    for (int32_t px = xMin; px <= xMax; px++) {
        if (level->getBlockId(px, yMax, tz) != OBSIDIAN) return false;
    }
    return true;
}

void ItemFlintSteel::fillPortalX(level::Level* level,
                                 int32_t xMin, int32_t xMax,
                                 int32_t yBase, int32_t yMax,
                                 int32_t tz) {
    // Rellena el interior: desde xMin+1 hasta xMax-1, desde yBase+1 hasta yMax-1
    for (int32_t px = xMin + 1; px < xMax; px++) {
        for (int32_t py = yBase + 1; py < yMax; py++) {
            level->setBlock(px, py, tz, PORTAL_FRAME, 0); // id=90 MCPE portal
        }
    }
}

// ============================================================
// checkPortalFrameZ / fillPortalZ — eje Z
// mirrors: el bloque "z方向" de FlintSteel.php
// ============================================================
bool ItemFlintSteel::checkPortalFrameZ(level::Level* level,
                                       int32_t tx,
                                       int32_t zMin, int32_t zMax,
                                       int32_t yBase, int32_t yMax) {
    for (int32_t pz = zMin; pz <= zMax; pz++) {
        if (level->getBlockId(tx, yMax, pz) != OBSIDIAN) return false;
    }
    return true;
}

void ItemFlintSteel::fillPortalZ(level::Level* level,
                                 int32_t tx,
                                 int32_t zMin, int32_t zMax,
                                 int32_t yBase, int32_t yMax) {
    for (int32_t pz = zMin + 1; pz < zMax; pz++) {
        for (int32_t py = yBase + 1; py < yMax; py++) {
            level->setBlock(tx, py, pz, PORTAL_FRAME, 0);
        }
    }
}

// ============================================================
// tryCreateNetherPortal
// mirrors: la lógica completa de detección de marco en FlintSteel.php
// Dimensiones válidas: ancho 4-23, alto 5-23 (incluyendo el marco)
// ============================================================
bool ItemFlintSteel::tryCreateNetherPortal(level::Level* level, Player* player,
                                           int32_t tx, int32_t ty, int32_t tz) {
    // PMMP allows portal frames up to 23 wide × 23 tall. We cap every
    // expansion loop at 32 iterations so a malformed world (e.g. blocks
    // returning inconsistent ids due to a chunk reload race) can't
    // produce an infinite loop that freezes the main thread.
    constexpr int kMaxExpand = 32;

    // ---- Eje X ----
    // Expandir horizontalmente desde tx para encontrar el ancho del marco
    int32_t xMax = tx, xMin = tx;
    for (int i = 0; i < kMaxExpand; ++i) {
        if (level->getBlockId(xMax + 1, ty, tz) != OBSIDIAN) break;
        xMax++;
    }
    for (int i = 0; i < kMaxExpand; ++i) {
        if (level->getBlockId(xMin - 1, ty, tz) != OBSIDIAN) break;
        xMin--;
    }
    int32_t countX = xMax - xMin + 1;

    if (countX >= 4 && countX <= 23) {
        // Encontrar la altura del marco en ambos extremos X
        int32_t yMaxLeft = ty, yMaxRight = ty;
        for (int i = 0; i < kMaxExpand; ++i) {
            if (level->getBlockId(xMax, yMaxLeft + 1, tz) != OBSIDIAN) break;
            yMaxLeft++;
        }
        for (int i = 0; i < kMaxExpand; ++i) {
            if (level->getBlockId(xMin, yMaxRight + 1, tz) != OBSIDIAN) break;
            yMaxRight++;
        }

        // El techo válido es el mínimo de ambos lados − 1
        // mirrors: $y_max = min($x_max_y, $x_min_y) - 1
        int32_t yMax  = std::min(yMaxLeft, yMaxRight);
        int32_t countY = yMax - ty + 2;  // incluyendo el bloque base y el techo

        if (countY >= 5 && countY <= 23) {
            if (checkPortalFrameX(level, xMin, xMax, ty, yMax, tz)) {
                fillPortalX(level, xMin, xMax, ty, yMax, tz);
                applyDurability(player);
                return true;
            }
        }
    }

    // ---- Eje Z ----
    int32_t zMax = tz, zMin = tz;
    for (int i = 0; i < kMaxExpand; ++i) {
        if (level->getBlockId(tx, ty, zMax + 1) != OBSIDIAN) break;
        zMax++;
    }
    for (int i = 0; i < kMaxExpand; ++i) {
        if (level->getBlockId(tx, ty, zMin - 1) != OBSIDIAN) break;
        zMin--;
    }
    int32_t countZ = zMax - zMin + 1;

    if (countZ >= 4 && countZ <= 23) {
        int32_t yMaxFar  = ty, yMaxNear = ty;
        // Note: previous version of this code incorrectly called
        // getBlockId(tx, y, tz, zMax) with a 4th redundant argument that
        // doesn't exist in the Level::getBlockId(int,int,int) signature.
        // The correct call passes only (tx, y, zMax/zMin) — zMax/zMin
        // already encode the Z coordinate of the column we're scanning.
        for (int i = 0; i < kMaxExpand; ++i) {
            if (level->getBlockId(tx, yMaxFar  + 1, zMax) != OBSIDIAN) break;
            yMaxFar++;
        }
        for (int i = 0; i < kMaxExpand; ++i) {
            if (level->getBlockId(tx, yMaxNear + 1, zMin) != OBSIDIAN) break;
            yMaxNear++;
        }

        int32_t yMax  = std::min(yMaxFar, yMaxNear);
        int32_t countY = yMax - ty + 2;

        if (countY >= 5 && countY <= 23) {
            if (checkPortalFrameZ(level, tx, zMin, zMax, ty, yMax)) {
                fillPortalZ(level, tx, zMin, zMax, ty, yMax);
                applyDurability(player);
                return true;
            }
        }
    }

    return false;
}

// ============================================================
// onActivate — punto de entrada principal
// mirrors: FlintSteel::onActivate($level, $player, $block, $target, ...)
// ============================================================
bool ItemFlintSteel::onActivate(level::Level* level, Player* player,
                                int32_t bx, int32_t by, int32_t bz,  // bloque AIR destino
                                int32_t tx, int32_t ty, int32_t tz,  // bloque target
                                uint8_t /*face*/) {
    if (!level || !player) return false;

    uint8_t targetId = level->getBlockId(tx, ty, tz);

    // ---- CASO 1: target es OBSIDIAN → intentar crear portal del Nether ----
    // mirrors: if($target->getId() === Block::OBSIDIAN && $player->getServer()->netherEnabled)
    if (targetId == OBSIDIAN && Server::getInstance().netherEnabled()) {
        if (tryCreateNetherPortal(level, player, tx, ty, tz)) {
            return true;
        }
        // Si no se pudo crear portal, caer en la lógica de colocar fuego
    }

    // ---- CASO 2: el bloque destino es AIR y el target es sólido → colocar fuego ----
    // mirrors: if($block->getId() === self::AIR && ($target instanceof Solid))
    uint8_t airId = level->getBlockId(bx, by, bz);
    if (airId == AIR && targetId != AIR &&
        targetId != FLOWING_WATER && targetId != WATER &&
        targetId != FLOWING_LAVA  && targetId != LAVA) {

        // Colocar fuego
        level->setBlock(bx, by, bz, FIRE, 0);

        // Programar propagación — mirrors: $level->scheduleUpdate($block, ...)
        BlockFire::scheduleUpdate(level, bx, by, bz);

        // Broadcast UpdateBlockPacket a todos los jugadores cercanos
        if (auto* srv = player->server()) {
            auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
            ubp->x       = bx;
            ubp->z       = bz;
            ubp->y       = (uint8_t)by;
            ubp->blockId  = FIRE;
            ubp->blockData = 0;
            ubp->flags    = 0xFF;

            int32_t cx = bx >> 4, cz = bz >> 4;
            for (auto& other : srv->playersSnapshot()) {
                if (other && other->isTrackingChunk(cx, cz))
                    other->sendPacket(ubp);
            }
        }

        // Desgastar herramienta (survival only)
        applyDurability(player);
        return true;
    }

    return false;
}

} // namespace atomikpe::item
