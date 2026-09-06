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
// mirrors: PMMP FlintSteel.php lines 80-96 (count_up check + fill loop)
//
// PMMP walks the ceiling from x_min upward, counting how many
// contiguous OBSIDIAN blocks are at y=y_max. If count_up == count_x,
// the entire ceiling is obsidian and the portal is valid.
// ============================================================
bool ItemFlintSteel::checkPortalFrameX(level::Level* level,
                                       int32_t xMin, int32_t xMax,
                                       int32_t /*yBase*/, int32_t yMax,
                                       int32_t tz) {
    // PMMP lines 80-84: for($ux = $x_min; ($level->getBlock(... y=$y_max ...)->getId() == OBSIDIAN && $ux <= $x_max); $ux++){ $count_up++; }
    // The loop breaks as soon as either: the block isn't OBSIDIAN, OR
    // $ux > $x_max. So count_up == count_x only if every block from
    // x_min to x_max at y=y_max is OBSIDIAN.
    int32_t count_up = 0;
    for (int32_t ux = xMin; ux <= xMax; ++ux) {
        if (level->getBlockId(ux, yMax, tz) != OBSIDIAN) break;
        count_up++;
    }
    return count_up == (xMax - xMin + 1);
}

void ItemFlintSteel::fillPortalX(level::Level* level,
                                 int32_t xMin, int32_t xMax,
                                 int32_t yBase, int32_t yMax,
                                 int32_t tz) {
    // PMMP lines 86-90: for($px = $x_min + 1; $px < $x_max; $px++)
    //                   for($py = $ty + 1; $py < $y_max; $py++)
    //                       setBlock PORTAL at (px, py, tz)
    for (int32_t px = xMin + 1; px < xMax; px++) {
        for (int32_t py = yBase + 1; py < yMax; py++) {
            level->setBlock(px, py, tz, PORTAL_FRAME, 0);
        }
    }
}

// ============================================================
// checkPortalFrameZ / fillPortalZ — eje Z
// mirrors: PMMP FlintSteel.php lines 123-140 (z-axis equivalent)
// ============================================================
bool ItemFlintSteel::checkPortalFrameZ(level::Level* level,
                                       int32_t tx,
                                       int32_t zMin, int32_t zMax,
                                       int32_t /*yBase*/, int32_t yMax) {
    int32_t count_up = 0;
    for (int32_t uz = zMin; uz <= zMax; ++uz) {
        if (level->getBlockId(tx, yMax, uz) != OBSIDIAN) break;
        count_up++;
    }
    return count_up == (zMax - zMin + 1);
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
// mirrors: PMMP FlintSteel.php lines 52-143 — detección de marco de
// obsidiana 4×5 mínimo hasta 23×23 máximo, en ambos ejes X y Z.
//
// Estructura del algoritmo de PMMP:
//   1. Expandir horizontalmente desde el bloque clickado para encontrar
//      x_min..x_max (o z_min..z_max). PMMP usa for loops con condición
//      de parada natural (cuando deja de encontrar OBSIDIAN).
//   2. Para cada extremo (x_max y x_min), subir verticalmente contando
//      cuántos OBSIDIAN hay. PMMP empieza el for en $y=$ty y sube hasta
//      que deja de encontrar OBSIDIAN. La primera iteración SIEMPRE
//      cuenta el bloque base (que es OBSIDIAN por el check inicial).
//      Por eso el resultado es "$x_max_y = $ty + (número de bloques
//      OBSIDIAN arriba incluyendo el base)".
//   3. y_max = min(x_max_y, x_min_y) - 1
//   4. count_y = y_max - ty + 2 (incluyendo base + techo)
//   5. Verificar que count_up == count_x (todo el techo sea OBSIDIAN
//      contiguo desde x_min).
//   6. Si todo pasa, rellenar el interior con PORTAL blocks.
//
// Atomik extension: añadimos un cap explícito kMaxExpand=32 a cada
// for loop. PMMP no lo necesita porque su getBlock() nunca retorna
// resultados inconsistentes, pero Atomik puede tener races de carga
// de chunks que causarían un loop infinito. 32 es seguro porque PMMP
// limita el tamaño máximo a 23.
// ============================================================
bool ItemFlintSteel::tryCreateNetherPortal(level::Level* level, Player* player,
                                           int32_t tx, int32_t ty, int32_t tz) {
    constexpr int kMaxExpand = 32;  // PMMP caps at 23; we allow 32 for safety

    // ---- Eje X ----
    // PMMP lines 58-65: $x_max = $tx; for($x = $tx + 1; getBlock==OBSIDIAN; $x++){ $x_max++; }
    //                  $x_min = $tx; for($x = $tx - 1; getBlock==OBSIDIAN; $x--){ $x_min--; }
    int32_t xMax = tx;
    for (int32_t x = tx + 1; x <= tx + kMaxExpand; ++x) {
        if (level->getBlockId(x, ty, tz) != OBSIDIAN) break;
        xMax = x;
    }
    int32_t xMin = tx;
    for (int32_t x = tx - 1; x >= tx - kMaxExpand; --x) {
        if (level->getBlockId(x, ty, tz) != OBSIDIAN) break;
        xMin = x;
    }
    int32_t countX = xMax - xMin + 1;

    if (countX >= 4 && countX <= 23) {
        // PMMP lines 68-75: $x_max_y = $ty; for($y=$ty; getBlock(x_max,y,tz)==OBSIDIAN; $y++){ $x_max_y++; }
        // The loop starts at $y=$ty and increments $x_max_y WHILE the
        // block at (x_max, $y, tz) is OBSIDIAN. Since (x_max, $ty, tz)
        // is OBSIDIAN (we just verified it during horizontal expansion),
        // the first iteration always counts the base block.
        int32_t x_max_y = ty;
        for (int32_t y = ty; y <= ty + kMaxExpand; ++y) {
            if (level->getBlockId(xMax, y, tz) != OBSIDIAN) break;
            x_max_y = y + 1;  // PMMP increments AFTER the check, so x_max_y = y+1 when loop exits
        }
        int32_t x_min_y = ty;
        for (int32_t y = ty; y <= ty + kMaxExpand; ++y) {
            if (level->getBlockId(xMin, y, tz) != OBSIDIAN) break;
            x_min_y = y + 1;
        }

        // PMMP line 76: $y_max = min($x_max_y, $x_min_y) - 1
        int32_t yMax  = std::min(x_max_y, x_min_y) - 1;
        int32_t countY = yMax - ty + 2;

        if (countY >= 5 && countY <= 23) {
            if (checkPortalFrameX(level, xMin, xMax, ty, yMax, tz)) {
                fillPortalX(level, xMin, xMax, ty, yMax, tz);
                applyDurability(player);
                return true;
            }
        }
    }

    // ---- Eje Z ----
    // PMMP lines 100-110: equivalent Z-axis expansion
    int32_t zMax = tz;
    for (int32_t z = tz + 1; z <= tz + kMaxExpand; ++z) {
        if (level->getBlockId(tx, ty, z) != OBSIDIAN) break;
        zMax = z;
    }
    int32_t zMin = tz;
    for (int32_t z = tz - 1; z >= tz - kMaxExpand; --z) {
        if (level->getBlockId(tx, ty, z) != OBSIDIAN) break;
        zMin = z;
    }
    int32_t countZ = zMax - zMin + 1;

    if (countZ >= 4 && countZ <= 23) {
        int32_t z_max_y = ty;
        for (int32_t y = ty; y <= ty + kMaxExpand; ++y) {
            if (level->getBlockId(tx, y, zMax) != OBSIDIAN) break;
            z_max_y = y + 1;
        }
        int32_t z_min_y = ty;
        for (int32_t y = ty; y <= ty + kMaxExpand; ++y) {
            if (level->getBlockId(tx, y, zMin) != OBSIDIAN) break;
            z_min_y = y + 1;
        }

        int32_t yMax = std::min(z_max_y, z_min_y) - 1;
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
// mirrors: PMMP FlintSteel.php lines 51-164
//
// PMMP onActivate flow:
//   1. If target is OBSIDIAN && netherEnabled, try to create a nether
//      portal. If success, return true.
//   2. Otherwise (or if portal creation failed), if block is AIR &&
//      target is Solid, place fire + schedule update + use durability.
//   3. Otherwise return false.
//
// PMMP line 145: if($block->getId() === self::AIR && ($target instanceof Solid))
// PMMP lines 146-153: setBlock fire; re-fetch block; if down is solid OR
//                     canNeighborBurn, scheduleUpdate + useOn.
//
// "Solid" in PMMP is a class hierarchy check — any non-liquid, non-air
// block that isn't explicitly a non-solid (like Flowable). We approximate
// by checking targetId != AIR and != liquids, which is the same set.
// ============================================================
bool ItemFlintSteel::onActivate(level::Level* level, Player* player,
                                int32_t bx, int32_t by, int32_t bz,  // bloque AIR destino
                                int32_t tx, int32_t ty, int32_t tz,  // bloque target
                                uint8_t /*face*/) {
    if (!level || !player) return false;

    uint8_t targetId = level->getBlockId(tx, ty, tz);

    // ---- CASO 1: target es OBSIDIAN → intentar crear portal del Nether ----
    // mirrors: PMMP line 52: if($target->getId() === Block::OBSIDIAN && $player->getServer()->netherEnabled)
    if (targetId == OBSIDIAN && Server::getInstance().netherEnabled()) {
        if (tryCreateNetherPortal(level, player, tx, ty, tz)) {
            return true;
        }
        // Si no se pudo crear portal, caer en la lógica de colocar fuego
        // (PMMP no tiene un else explícito — simplemente continúa al siguiente if)
    }

    // ---- CASO 2: el bloque destino es AIR y el target es sólido → colocar fuego ----
    // mirrors: PMMP line 145: if($block->getId() === self::AIR && ($target instanceof Solid))
    //
    // "Solid" en PMMP es cualquier bloque que no sea Flowable/Liquid/Air.
    // Aproximamos verificando que targetId no sea AIR ni líquidos.
    uint8_t airId = level->getBlockId(bx, by, bz);
    if (airId == AIR && targetId != AIR &&
        targetId != FLOWING_WATER && targetId != WATER &&
        targetId != FLOWING_LAVA  && targetId != LAVA) {

        // Colocar fuego con meta=0
        // mirrors: PMMP line 146: $level->setBlock($block, new Fire(), true)
        level->setBlock(bx, by, bz, FIRE, 0);

        // PMMP lines 148-153:
        //   $block = $level->getBlock($block);   // re-fetch as Fire instance
        //   if($block->getSide(DOWN)->isTopFacingSurfaceSolid() || $block->canNeighborBurn()){
        //       $level->scheduleUpdate($block, $block->getTickRate() + mt_rand(0, 10));
        //   }
        //
        // Solo scheduleamos el update si hay suelo sólido abajo O vecinos
        // inflamables. Si no, el fuego se extinguirá en el primer tick
        // del onUpdate (que es lo que PMMP hace también).
        bool has_support = BlockFire::isTopFacingSurfaceSolid(level, bx, by - 1, bz) ||
                           BlockFire::canNeighborBurn(level, bx, by, bz);
        if (has_support) {
            BlockFire::scheduleUpdate(level, bx, by, bz);
        }

        // Broadcast UpdateBlockPacket a todos los jugadores cercanos
        // (Atomik extension — PMMP hace esto internamente via Level::setBlock)
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
        // mirrors: PMMP lines 155-158: if($player->isSurvival()){ $this->useOn($block, 2); ... }
        applyDurability(player);
        return true;
    }

    return false;
}

} // namespace atomikpe::item
