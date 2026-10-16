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
// BlockFire.h — Portado de block/Fire (the reference server)
//
// Implementa:
//   - Luz nivel 15
//   - canBeReplaced / canWalkThrough / hardness 0
//   - Daño a entidades (onEntityCollide)
//   - Propagación por ticks programados (scheduleUpdate / onScheduledUpdate)
//   - Fuego eterno sobre Netherrack
//   - Extinción por lluvia, edad > 3 sin suelo sólido, etc.
//
// Diferencias con el original (adaptadas a la arquitectura C++ de CurseStone):
//   - scheduleUpdate() usa ServerScheduler::scheduleSync() en vez de
//     la cola interna del servidor de referencia.
//   - World no expone getBlockData() todavía; se accede via chunks() directamente.
//     Agrega `uint8_t getBlockData(int32_t x, int32_t y, int32_t z)` a World.h
//     (ver parche incluido abajo en Level_patch.h).
//   - fireSpreadEnabled() y netherEnabled() son métodos nuevos en Server.h
//     (ver parche Server_patch.h).
//   - La detección de portal de obsidiana pertenece a ItemFlintSteel.
#pragma once
#include "BlockTransparent.h"
#include "BlockIds.h"
#include <cstdint>
#include <string>

namespace cursestone {
namespace world {
class World;
}
namespace entity {
class Entity;
}
}

namespace cursestone::block {

class BlockFire : public BlockTransparent {
  public:
    // ---- Identidad ----
    uint8_t id() const override {
        return FIRE;
    }
    std::string name() const override {
        return "Fire Block";
    }

    // ---- Propiedades físicas / de render ----
    bool isSolid() const override {
        return false;
    }
    bool isTransparent() const override {
        return true;
    }
    bool canBeReplaced() const override {
        return true;
    } // The reference server: return true
    bool canWalkThrough() const override {
        return true;
    }
    float getHardness() const override {
        return 0.0f;
    }
    uint8_t getLightLevel() const override {
        return 15;
    } // The reference server: return 15

    // ---- Interacción con entidades ----
    // Llama el entity-tick loop por cada entidad que se superpone con este bloque.
    // Mirrors the reference server Fire::onEntityCollide(entity)
    static void onEntityCollide(world::World* world, int32_t bx, int32_t by, int32_t bz,
                                entity::Entity* entity);

    // ---- Tick programado ----
    // Encola el siguiente update de propagación/extinción para (x,y,z).
    // delay = TICK_RATE + rand(0..10)  — idéntico a the reference server scheduleUpdate.
    static void scheduleUpdate(world::World* world, int32_t x, int32_t y, int32_t z);

    // Lógica de propagación + extinción, llamada por FireUpdateTask::onRun().
    // Mirrors the reference server Fire::onUpdate(BLOCK_UPDATE_SCHEDULED)
    static void onScheduledUpdate(world::World* world, int32_t x, int32_t y, int32_t z);

    static constexpr int TICK_RATE = 30; // ticks (= 1.5 s a 20 TPS)

    // ---- Tablas de flamabilidad (the reference server verbatim) ----
    // Retornan 0 si el bloque no es inflamable.
    static int getBurnAbility(uint8_t blockId); // probabilidad de consumirse
    static int getBurnChance(uint8_t blockId);  // probabilidad de propagarse

    // ---- Helpers de geometría ----
    static bool isTopFacingSurfaceSolid(world::World* world, int32_t x, int32_t y, int32_t z);
    static bool canNeighborBurn(world::World* world, int32_t x, int32_t y, int32_t z);

  private:
    static void tryToCatchBlockOnFire(world::World* world, int32_t tx, int32_t ty, int32_t tz,
                                      int bound, int damage);

    static int getChanceOfNeighborsEncouragingFire(world::World* world, int32_t x, int32_t y,
                                                   int32_t z);
};

} // namespace cursestone::block
