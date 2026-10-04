// ItemFlintSteel.cpp — Simplified flint & steel.
//
// DESIGN DECISION (v6):
//   Per user request, the flint & steel should ONLY place a fire block.
//   No nether portal creation. No fire spread. No aging. No durability
//   tracking for now (kept because removing it would require touching
//   PlayerInventory API; the function is just a no-op for creative
//   players).
//
//   This file is intentionally short. All the PMMP portal-detection
//   logic from v5 has been deleted — if you want to restore it later,
//   see PMMP_REPLICA.md for the v5 design notes.
#include "ItemFlintSteel.h"
#include "ItemStack.h"
#include "block/BlockFire.h"
#include "block/BlockIds.h"
#include "world/World.h"
#include "Player.h"
#include "inventory/PlayerInventory.h"
#include "network/protocol/UpdateBlockPacket.h"
#include "Server.h"
#include <memory>

namespace cursestone::item {

using namespace cursestone::block;

// ============================================================
// applyDurability
// mirrors: $this->useOn($block, 2) — sums 2 to meta, breaks the item
// if it exceeds MAX_DURABILITY.
//
// Retained from v5 because removing it would require changing the
// header signature, and the function already no-ops for creative
// players (isSurvival() check). Survival durability tracking is a
// nice-to-have that doesn't contribute to the freeze.
// ============================================================
void ItemFlintSteel::applyDurability(Player* player) {
    if (!player || !player->isSurvival()) return;

    auto inv = player->getInventory();
    if (!inv) return;

    auto held = inv->getHeldItem();
    if (held.isEmpty() || held.id != 259) return;

    held.meta += 2;
    if (held.meta > MAX_DURABILITY) {
        inv->clear(inv->heldSlot());
    } else {
        inv->setItemInHand(held);
    }
    // NOTE: no manual sendSlot() here — both clear() and
    // setItemInHand() (→ PlayerInventory::setItem()) already ran
    // PlayerInventory::onSlotChange(), which sends the confirmation
    // ContainerSetSlotPacket itself. See the same fix/comment in
    // Player.cpp's CONTAINER_SET_SLOT_PACKET handler.
}

// ============================================================
// checkPortalFrameX/Z, fillPortalX/Z — DEAD in v6.
// Retained as stubs so ItemFlintSteel.h's declaration compiles.
// ============================================================
bool ItemFlintSteel::checkPortalFrameX(world::World* /*world*/,
                                       int32_t /*xMin*/, int32_t /*xMax*/,
                                       int32_t /*yBase*/, int32_t /*yMax*/,
                                       int32_t /*tz*/) {
    return false;  // portal creation disabled in v6
}

void ItemFlintSteel::fillPortalX(world::World* /*world*/,
                                 int32_t /*xMin*/, int32_t /*xMax*/,
                                 int32_t /*yBase*/, int32_t /*yMax*/,
                                 int32_t /*tz*/) {
    // No-op.
}

bool ItemFlintSteel::checkPortalFrameZ(world::World* /*world*/,
                                       int32_t /*tx*/,
                                       int32_t /*zMin*/, int32_t /*zMax*/,
                                       int32_t /*yBase*/, int32_t /*yMax*/) {
    return false;
}

void ItemFlintSteel::fillPortalZ(world::World* /*world*/,
                                 int32_t /*tx*/,
                                 int32_t /*zMin*/, int32_t /*zMax*/,
                                 int32_t /*yBase*/, int32_t /*yMax*/) {
    // No-op.
}

// ============================================================
// tryCreateNetherPortal — DISABLED in v6.
// ============================================================
bool ItemFlintSteel::tryCreateNetherPortal(world::World* /*world*/, Player* /*player*/,
                                           int32_t /*tx*/, int32_t /*ty*/, int32_t /*tz*/) {
    return false;  // nether portal creation disabled in v6
}

// ============================================================
// onActivate — punto de entrada principal (versión simplificada v6).
//
// Flujo:
//   1. Verifica que el bloque destino (bx,by,bz) sea AIR.
//   2. Verifica que el bloque target (tx,ty,tz) sea sólido (no AIR,
//      no líquidos). Esto evita colocar fuego en el aire libre.
//   3. Coloca el bloque FIRE con meta=0.
//   4. Broadcast UpdateBlockPacket a los jugadores cercanos.
//   5. Desgasta la herramienta (survival only).
//
// NO se llama a scheduleUpdate() — el fuego es un bloque visual
// estático. No se propaga, no envejece, no se apaga solo.
// ============================================================
bool ItemFlintSteel::onActivate(world::World* world, Player* player,
                                int32_t bx, int32_t by, int32_t bz,
                                int32_t tx, int32_t ty, int32_t tz,
                                uint8_t /*face*/) {
    if (!world || !player) return false;

    uint8_t targetId = world->getBlockId(tx, ty, tz);
    uint8_t airId    = world->getBlockId(bx, by, bz);

    // Solo colocar fuego si el destino es AIR y el target es sólido.
    // mirrors PMMP line 145: if($block->getId() === self::AIR && ($target instanceof Solid))
    if (airId != AIR) return false;
    if (targetId == AIR) return false;
    if (targetId == FLOWING_WATER || targetId == WATER) return false;
    if (targetId == FLOWING_LAVA  || targetId == LAVA)  return false;

    // Colocar el bloque FIRE con meta=0.
    world->setBlock(bx, by, bz, FIRE, 0);

    // Broadcast UpdateBlockPacket a los jugadores cercanos.
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
            if (other && other->canSeeBlock(cx, cz))
                other->sendPacket(ubp);
        }
    }

    // Desgastar herramienta (survival only).
    applyDurability(player);
    return true;
}

} // namespace cursestone::item
