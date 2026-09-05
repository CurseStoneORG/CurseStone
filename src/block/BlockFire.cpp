// BlockFire.cpp — Portado de pocketmine/block/Fire.php (PMMP)
// Ver BlockFire.h para notas de porte y diferencias con el original PHP.
#include "BlockFire.h"
#include "BlockIds.h"
#include "level/Level.h"
#include "level/format/Chunk.h"    // mcp_chunk_get_block_data
#include "Server.h"
#include "scheduler/ServerScheduler.h"
#include "scheduler/Task.h"
#include "entity/Entity.h"
#include <cstdlib>    // rand()
#include <algorithm>  // std::min / std::max
#include <memory>

namespace atomikpe::block {

// ============================================================
// FireUpdateTask — tarea one-shot que ejecuta onScheduledUpdate()
// ============================================================
namespace {

struct FireUpdateTask : public scheduler::Task {
    level::Level* level = nullptr;
    int32_t x = 0, y = 0, z = 0;

    void onRun() override {
        // El bloque puede haber sido reemplazado entre la programación
        // y la ejecución del tick — verificamos primero.
        if (level && level->getBlockId(x, y, z) == FIRE) {
            BlockFire::onScheduledUpdate(level, x, y, z);
        }
    }
};

} // anonymous namespace

// ============================================================
// scheduleUpdate
// mirrors: $level->scheduleUpdate($block, $block->getTickRate() + mt_rand(0,10))
// ============================================================
void BlockFire::scheduleUpdate(level::Level* level,
                               int32_t x, int32_t y, int32_t z) {
    int delay = TICK_RATE + (rand() % 11);  // 30..40 ticks
    auto task = std::make_shared<FireUpdateTask>();
    task->level = level;
    task->x = x; task->y = y; task->z = z;
    scheduler::ServerScheduler::get().scheduleSync(task, delay);
}

// ============================================================
// Tablas de flamabilidad — valores PMMP verbatim
// getBurnAbility: probabilidad de que el bloque se consuma
// getBurnChance:  probabilidad de que el fuego salte a él
// ============================================================
int BlockFire::getBurnAbility(uint8_t id) {
    switch (id) {
        case PLANKS:      return 20;
        case LOG:         return 5;
        case LEAVES:      return 60;
        case BOOKSHELF:   return 30;
        case TNT:         return 15;
        case WOOL:        return 30;
        case TALL_GRASS:  return 60;
        case DEAD_BUSH:   return 60;
        case FENCE:       return 20;
        case VINE:        return 15;
        case WOOD_STAIRS: return 20;  // OAK_STAIRS
        case 126:         return 20;  // SLAB_WOOD
        default:          return 0;
    }
}

int BlockFire::getBurnChance(uint8_t id) {
    switch (id) {
        case PLANKS:      return 5;
        case LOG:         return 5;
        case LEAVES:      return 30;
        case BOOKSHELF:   return 30;
        case TNT:         return 15;
        case WOOL:        return 10;
        case TALL_GRASS:  return 60;
        case DEAD_BUSH:   return 60;
        case FENCE:       return 5;
        case VINE:        return 15;
        case WOOD_STAIRS: return 5;
        case 126:         return 5;
        default:          return 0;
    }
}

// ============================================================
// isTopFacingSurfaceSolid
// Aproximación: cualquier bloque no-fluido, no-aire, no-fuego.
// mirrors: PMMP Block::isTopFacingSurfaceSolid()
// ============================================================
bool BlockFire::isTopFacingSurfaceSolid(level::Level* level,
                                        int32_t x, int32_t y, int32_t z) {
    if (y < 0 || y >= 256) return false;
    uint8_t bid = level->getBlockId(x, y, z);
    switch (bid) {
        case AIR: case FIRE:
        case TALL_GRASS: case DEAD_BUSH:
        case FLOWING_WATER: case WATER:
        case FLOWING_LAVA:  case LAVA:
            return false;
        default:
            return true;
    }
}

// ============================================================
// canNeighborBurn — true si algún vecino de los 6 lados puede arder
// mirrors: PMMP Fire::canNeighborBurn()
// ============================================================
bool BlockFire::canNeighborBurn(level::Level* level,
                                int32_t x, int32_t y, int32_t z) {
    // dx, dy, dz por cara (SIDE_EAST, WEST, UP, DOWN, SOUTH, NORTH)
    static const int dx[6] = { 1,-1, 0, 0, 0, 0};
    static const int dy[6] = { 0, 0, 1,-1, 0, 0};
    static const int dz[6] = { 0, 0, 0, 0, 1,-1};
    for (int i = 0; i < 6; i++) {
        int ny = y + dy[i];
        if (ny < 0 || ny >= 256) continue;
        if (getBurnAbility(level->getBlockId(x + dx[i], ny, z + dz[i])) > 0)
            return true;
    }
    return false;
}

// ============================================================
// getChanceOfNeighborsEncouragingFire
// mirrors: PMMP Fire::getChanceOfNeighborsEncouragingFire($block)
// Precondición: el bloque en (x,y,z) debe ser AIR.
// ============================================================
int BlockFire::getChanceOfNeighborsEncouragingFire(level::Level* level,
                                                   int32_t x, int32_t y, int32_t z) {
    if (level->getBlockId(x, y, z) != AIR) return 0;
    static const int dx[6] = { 1,-1, 0, 0, 0, 0};
    static const int dy[6] = { 0, 0, 1,-1, 0, 0};
    static const int dz[6] = { 0, 0, 0, 0, 1,-1};
    int chance = 0;
    for (int i = 0; i < 6; i++) {
        int ny = y + dy[i];
        if (ny < 0 || ny >= 256) continue;
        int c = getBurnChance(level->getBlockId(x + dx[i], ny, z + dz[i]));
        if (c > chance) chance = c;
    }
    return chance;
}

// ============================================================
// tryToCatchBlockOnFire
// mirrors: PMMP Fire::tryToCatchBlockOnFire($block, $bound, $damage)
// ============================================================
void BlockFire::tryToCatchBlockOnFire(level::Level* level,
                                      int32_t tx, int32_t ty, int32_t tz,
                                      int bound, int damage) {
    if (ty < 0 || ty >= 256) return;
    int burnAbility = getBurnAbility(level->getBlockId(tx, ty, tz));
    if (burnAbility == 0) return;

    if ((rand() % bound) < burnAbility) {
        if ((rand() % (damage + 10)) < 5) {
            // Ignita el bloque vecino con meta escalado
            // mirrors: max(15, $damage + mt_rand(0,4)/4)
            int meta = std::min(15, damage + (rand() % 5) / 4);
            // TODO: disparar BlockBurnEvent y cancelar si necesario
            level->setBlock(tx, ty, tz, FIRE, (uint8_t)meta);
            scheduleUpdate(level, tx, ty, tz);
        } else {
            // El fuego consume el bloque pero se apaga en ese punto
            // mirrors: $this->getLevel()->setBlock($this, new Air(), true)
            level->setBlock(tx, ty, tz, AIR, 0);
        }
        // TODO: TNT::prime() si el bloque quemado era TNT
    }
}

// ============================================================
// onEntityCollide
// mirrors: PMMP Fire::onEntityCollide($entity)
// ============================================================
void BlockFire::onEntityCollide(level::Level* /*level*/,
                                int32_t /*bx*/, int32_t /*by*/, int32_t /*bz*/,
                                entity::Entity* entity) {
    if (!entity) return;
    // TODO: ignorar si entity tiene efecto FIRE_RESISTANCE
    // TODO: disparar EntityDamageByBlockEvent(CAUSE_FIRE, 1) + useArmors()
    entity->attack(1);      // 1 HP de daño por tick
    // TODO: disparar EntityCombustByBlockEvent, cancelar si es Arrow
    entity->setOnFire(8);   // 8 segundos, mirrors PMMP
}

// ============================================================
// onScheduledUpdate — núcleo de la propagación del fuego
// mirrors: PMMP Fire::onUpdate(BLOCK_UPDATE_SCHEDULED)
// ============================================================
void BlockFire::onScheduledUpdate(level::Level* level,
                                  int32_t x, int32_t y, int32_t z) {
    // Verificar que siga siendo fuego (puede haber sido apagado)
    if (level->getBlockId(x, y, z) != FIRE) return;

    // Obtener meta actual (el "nivel de edad" del fuego, 0..15)
    // Necesita Level::getBlockData() — ver Level_patch.h
    uint8_t meta = level->getBlockData(x, y, z);

    // --- ¿Fuego eterno? Solo sobre Netherrack ---
    // mirrors: $forever = $this->getSide(SIDE_DOWN)->getId() == NETHERRACK
    bool forever = (y > 0 &&
                    level->getBlockId(x, y - 1, z) == NETHERRACK);

    // --- Extinción inmediata: sin suelo sólido Y sin vecino inflamable ---
    if (!isTopFacingSurfaceSolid(level, x, y - 1, z) &&
        !canNeighborBurn(level, x, y, z)) {
        level->setBlock(x, y, z, AIR, 0);
        return;
    }

    // --- Lluvia: apagar si el cielo está visible ---
    // TODO: level->isRainy() y level->canBlockSeeSky()
    // if (!forever && level->isRainy() && level->canBlockSeeSky(x,y,z)) { ... }

    // --- Envejecer el fuego (meta 0→15) ---
    // mirrors: $this->meta = $meta + mt_rand(0,3)
    if (meta < 15) {
        uint8_t newMeta = (uint8_t)std::min(15, (int)meta + (rand() % 4));
        level->setBlock(x, y, z, FIRE, newMeta);
        meta = newMeta;
    }

    // --- Programar el siguiente tick ---
    scheduleUpdate(level, x, y, z);

    // --- Si fireSpread está deshabilitado, terminar aquí ---
    // Necesita Server::fireSpreadEnabled() — ver Server_patch.h
    if (!Server::getInstance().fireSpreadEnabled()) return;

    // --- Extinción por edad + suelo no sólido ---
    // mirrors: if(!$forever && !$this->canNeighborBurn())
    if (!forever && !canNeighborBurn(level, x, y, z)) {
        if (!isTopFacingSurfaceSolid(level, x, y - 1, z) || meta > 3) {
            level->setBlock(x, y, z, AIR, 0);
            return;
        }
    }

    // --- Extinción aleatoria cuando el suelo no es inflamable y es viejo ---
    // mirrors: if(!$forever && !($this->getSide(DOWN)->getBurnAbility() > 0)
    //              && $meta >= 15 && mt_rand(0,4) == 0)
    if (!forever &&
        getBurnAbility(level->getBlockId(x, y - 1, z)) == 0 &&
        meta >= 15 && (rand() % 5) == 0) {
        level->setBlock(x, y, z, AIR, 0);
        return;
    }

    // --- Propagación a los 6 lados inmediatos ---
    // mirrors: $this->tryToCatchBlockOnFire($this->getSide(SIDE_*), bound, $meta)
    int o = 0; // TODO: reducir 'o' con intensidad de lluvia
    int diff = Server::getInstance().difficulty();

    tryToCatchBlockOnFire(level, x + 1, y,     z,     300 + o, meta);
    tryToCatchBlockOnFire(level, x - 1, y,     z,     300 + o, meta);
    tryToCatchBlockOnFire(level, x,     y - 1, z,     250 + o, meta);
    tryToCatchBlockOnFire(level, x,     y + 1, z,     250 + o, meta);
    tryToCatchBlockOnFire(level, x,     y,     z + 1, 300 + o, meta);
    tryToCatchBlockOnFire(level, x,     y,     z - 1, 300 + o, meta);

    // --- Propagación de largo alcance (3×7×3 volumen) ---
    // mirrors: el triple-for de PMMP con $k = 100 + (y - (this->y+1))*100
    for (int bx = x - 1; bx <= x + 1; bx++) {
        for (int bz = z - 1; bz <= z + 1; bz++) {
            for (int by = y - 1; by <= y + 4; by++) {
                if (by < 0 || by >= 256) continue;

                int k = 100;
                if (by > y + 1) k += (by - (y + 1)) * 100;

                int chance = getChanceOfNeighborsEncouragingFire(
                                 level, bx, by, bz);
                if (chance <= 0) continue;

                // mirrors: $t = ($chance + 40 + $server->getDifficulty() * 7)
                int t = chance + 40 + diff * 7;
                // TODO: reducir t durante lluvia
                if (t > 0 && (rand() % k) <= t) {
                    int dmg = std::min(15, (int)meta + (rand() % 5) / 4);
                    level->setBlock(bx, by, bz, FIRE, (uint8_t)dmg);
                    scheduleUpdate(level, bx, by, bz);
                }
            }
        }
    }
}

} // namespace atomikpe::block
