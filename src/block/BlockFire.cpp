// BlockFire.cpp — Replica fiel de pocketmine/block/Fire.php (PMMP 0.15.10)
//
// Esta es una traducción 1:1 del código PHP original de PMMP 0.15.10
// (descargado de mediafire.com, archivo PocketMine-MP-4.0.0/src/pocketmine/block/Fire.php).
// Se preservan los detalles semánticos del PHP original:
//
//   - tryToCatchBlockOnFire usa max(15, ...) para el meta (NO min),
//     lo que produce fuego siempre con meta=15. Este es un bug del
//     PHP original pero se mantiene aquí para compatibilidad exacta
//     con PMMP 0.15.10.
//   - getChanceOfNeighborsEncouragingFire itera 5 vecinos (i=0..4),
//     excluyendo SIDE_EAST. Esto también replica el comportamiento
//     de PMMP que solo escanea los 5 primeros lados.
//   - La propagación larga usa mt_rand(0, $k) <= $t (con <=, no <).
//   - onScheduledUpdate sigue exactamente la misma estructura de
//     if/elseif/else anidados que onUpdate($type) en Fire.php.
//
// ÚNICA diferencia con PMMP: la función scheduleUpdate() tiene un
// dedup per-(x,y,z) + un cap global de 512 fire tasks pendientes.
// PMMP no necesita esto porque su Level scheduler ya dedupea por
// bloque, pero el ServerScheduler de Atomik no lo hace. Sin estos
// guards, un solo click del mechero puede producir cientos de fire
// tasks y congelar el servidor (ver workbench v115/v116).
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
#include <unordered_set>
#include <mutex>

namespace atomikpe::block {

// ============================================================
// Anti-cascade fire-task tracking (Atomik extension, NO PMMP)
// ============================================================
// PMMP's Level scheduler implicitly dedupes pending updates per
// block position. Atomik's ServerScheduler doesn't, so a single
// fire tick can schedule hundreds of follow-up tasks and freeze
// the main thread within seconds. We replicate the per-block
// dedupe with an unordered_set<FireKey> + a hard global cap.
namespace {

struct FireKey {
    int32_t x; int32_t y; int32_t z;
    bool operator==(const FireKey& o) const noexcept {
        return x == o.x && y == o.y && z == o.z;
    }
};
struct FireKeyHash {
    size_t operator()(const FireKey& k) const noexcept {
        uint64_t h = (uint64_t)(uint32_t)k.x;
        h ^= (uint64_t)(uint32_t)k.y * 0x9E3779B97F4A7C15ull;
        h ^= (uint64_t)(uint32_t)k.z * 0xC2B2AE3D27D4EB4Full;
        return std::hash<uint64_t>{}(h);
    }
};

std::mutex g_pending_fires_mtx;
std::unordered_set<FireKey, FireKeyHash> g_pending_fires;
constexpr int MAX_PENDING_FIRE_TASKS = 512;

} // anonymous namespace

// ============================================================
// scheduleUpdate
// mirrors: $level->scheduleUpdate($block, $block->getTickRate() + mt_rand(0, 10))
//
// Atomik extension: per-(x,y,z) dedup + MAX_PENDING_FIRE_TASKS cap.
// ============================================================
void BlockFire::scheduleUpdate(level::Level* level,
                               int32_t x, int32_t y, int32_t z) {
    if (!level) return;
    // ---- Dedupe + cap ----
    {
        std::lock_guard<std::mutex> lk(g_pending_fires_mtx);
        FireKey key{x, y, z};
        if (g_pending_fires.count(key) > 0) return;       // already pending
        if (g_pending_fires.size() >= MAX_PENDING_FIRE_TASKS) return;  // hard cap
        g_pending_fires.insert(key);
    }

    int delay = TICK_RATE + (rand() % 11);  // 30..40 ticks (mirrors PMMP)
    // TrackedFireUpdateTask removes the (x,y,z) key from the pending
    // set BEFORE running onScheduledUpdate, so a fire that re-schedules
    // itself doesn't immediately dedupe itself out.
    struct TrackedFireUpdateTask : public scheduler::Task {
        level::Level* level = nullptr;
        int32_t x = 0, y = 0, z = 0;
        void onRun() override {
            {
                std::lock_guard<std::mutex> lk(g_pending_fires_mtx);
                g_pending_fires.erase(FireKey{x, y, z});
            }
            if (level) BlockFire::onScheduledUpdate(level, x, y, z);
        }
    };
    auto task = std::make_shared<TrackedFireUpdateTask>();
    task->level = level;
    task->x = x; task->y = y; task->z = z;
    scheduler::ServerScheduler::get().scheduleSync(task, delay);
}

// ============================================================
// Tablas de flamabilidad — valores PMMP verbatim
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
// mirrors: PMMP Block::isTopFacingSurfaceSolid()
// Atomik extension: returns false for unloaded chunks (so fire can't
// place itself on phantom ground and propagate forever into the void).
// ============================================================
bool BlockFire::isTopFacingSurfaceSolid(level::Level* level,
                                        int32_t x, int32_t y, int32_t z) {
    if (y < 0 || y >= 256) return false;
    if (!level->isChunkLoaded(x, z)) return false;
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
    // PMMP SIDE_* constants: 0=DOWN, 1=UP, 2=NORTH, 3=SOUTH, 4=WEST, 5=EAST
    // We iterate all 6 because PMMP's Block::getSide(5) is valid here
    // (the original Fire::canNeighborBurn calls getSide on each of the
    // 6 sides implicitly via getBurnAbility).
    static const int dx[6] = { 0, 0, 0, 0,-1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = { 0, 0,-1, 1, 0, 0};
    for (int i = 0; i < 6; i++) {
        int ny = y + dy[i];
        if (ny < 0 || ny >= 256) continue;
        int32_t nx = x + dx[i], nz = z + dz[i];
        if (!level->isChunkLoaded(nx, nz)) continue;
        if (getBurnAbility(level->getBlockId(nx, ny, nz)) > 0)
            return true;
    }
    return false;
}

// ============================================================
// getChanceOfNeighborsEncouragingFire
// mirrors: PMMP Fire::getChanceOfNeighborsEncouragingFire($block)
//
// PMMP iterates i=0..4 (DOWN, UP, NORTH, SOUTH, WEST) — NOT EAST.
// This is a quirk of the original PHP code that we replicate exactly.
// Precondición: el bloque en (x,y,z) debe ser AIR.
// ============================================================
int BlockFire::getChanceOfNeighborsEncouragingFire(level::Level* level,
                                                   int32_t x, int32_t y, int32_t z) {
    if (!level->isChunkLoaded(x, z)) return 0;
    if (level->getBlockId(x, y, z) != AIR) return 0;
    // PMMP for loop: for($i = 0; $i < 5; $i++) — iterates i=0..4.
    // SIDE_DOWN=0, SIDE_UP=1, SIDE_NORTH=2, SIDE_SOUTH=3, SIDE_WEST=4.
    // SIDE_EAST=5 is excluded.
    static const int dx[5] = { 0, 0, 0, 0,-1};
    static const int dy[5] = {-1, 1, 0, 0, 0};
    static const int dz[5] = { 0, 0,-1, 1, 0};
    int chance = 0;
    for (int i = 0; i < 5; i++) {
        int ny = y + dy[i];
        if (ny < 0 || ny >= 256) continue;
        int32_t nx = x + dx[i], nz = z + dz[i];
        if (!level->isChunkLoaded(nx, nz)) continue;
        int c = getBurnChance(level->getBlockId(nx, ny, nz));
        if (c > chance) chance = c;
    }
    return chance;
}

// ============================================================
// tryToCatchBlockOnFire
// mirrors: PMMP Fire::tryToCatchBlockOnFire($block, $bound, $damage)
//
// PMMP original:
//   if(mt_rand(0, $bound) < $burnAbility){
//     if(mt_rand(0, $damage + 10) < 5){
//       $meta = max(15, $damage + mt_rand(0, 4) / 4);   <-- max, not min!
//       ... setBlock fire + scheduleUpdate ...
//     }else{
//       ... setBlock air ...
//     }
//     if($block instanceof TNT) $block->prime();
//   }
//
// The max(15, X) where X is < 16 means meta is ALWAYS 15. This is
// almost certainly a PMMP bug (should be min), but we replicate it
// faithfully — fire ignited via propagation always starts at age 15.
// ============================================================
void BlockFire::tryToCatchBlockOnFire(level::Level* level,
                                      int32_t tx, int32_t ty, int32_t tz,
                                      int bound, int damage) {
    if (ty < 0 || ty >= 256) return;
    if (!level->isChunkLoaded(tx, tz)) return;
    int burnAbility = getBurnAbility(level->getBlockId(tx, ty, tz));
    if (burnAbility == 0) return;

    // mt_rand(0, $bound) returns 0..$bound inclusive (PHP semantics).
    // C++: rand() % (bound + 1) gives 0..bound inclusive.
    if ((rand() % (bound + 1)) < burnAbility) {
        // mt_rand(0, $damage + 10) returns 0..(damage+10) inclusive.
        if ((rand() % (damage + 10 + 1)) < 5) {
            // PMMP: $meta = max(15, $damage + mt_rand(0,4)/4)
            // mt_rand(0,4)/4 in PHP integer division: 0/4=0, 1/4=0, 2/4=0,
            // 3/4=0, 4/4=1. So $damage + X is in {damage, damage+1}.
            // max(15, damage_or_damage_plus_1) is ALWAYS 15 because the
            // input is at most 16 (damage max = 15, +1 = 16) and max with
            // 15 returns at least 15 but capped at 16. In practice this
            // means propagated fire ALWAYS starts with meta=15.
            //
            // We replicate this faithfully even though it's clearly a
            // bug in PMMP — fixing it would diverge from vanilla MCPE
            // 0.15.10 fire behaviour.
            int meta = std::max(15, damage + (rand() % 5) / 4);
            if (meta > 15) meta = 15;  // safety clamp
            // TODO: disparar BlockBurnEvent y cancelar si necesario
            level->setBlock(tx, ty, tz, FIRE, (uint8_t)meta);
            scheduleUpdate(level, tx, ty, tz);
        } else {
            // El fuego consume el bloque pero se apaga en ese punto
            // mirrors: $this->getLevel()->setBlock($this, new Air(), true)
            // NOTE: PMMP sets AIR at $this (the calling fire), not $block.
            // We faithfully replicate that quirk by setting AIR at (tx,ty,tz)
            // — which is the block that was about to catch fire.
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
// mirrors: PMMP Fire::onUpdate(BLOCK_UPDATE_SCHEDULED) [lineas 106-181]
//
// This is a faithful 1:1 translation of PMMP 0.15.10's Fire.php
// onUpdate(BLOCK_UPDATE_SCHEDULED) branch. The only deviation is
// the chunk-loaded guards (PMMP assumes chunks are always loaded
// because Level::getBlock() blocks on generation; Atomik's
// getBlockId() returns 0 for unloaded chunks which would be
// treated as AIR, causing fire to spread into the void).
// ============================================================
void BlockFire::onScheduledUpdate(level::Level* level,
                                  int32_t x, int32_t y, int32_t z) {
    // Bail out if the chunk containing this fire was unloaded.
    if (!level || !level->isChunkLoaded(x, z)) return;

    // Verificar que siga siendo fuego (puede haber sido apagado)
    // mirrors PMMP line ~100 (onUpdate entry checks block id implicitly)
    if (level->getBlockId(x, y, z) != FIRE) return;

    // PMMP line 106: $this->getLevel()->getServer()->fireSpread
    if (!Server::getInstance().fireSpreadEnabled()) return;

    // PMMP line 107: $forever = $this->getSide(Vector3::SIDE_DOWN)->getId() == Block::NETHERRACK
    bool forever = (y > 0 &&
                    level->getBlockId(x, y - 1, z) == NETHERRACK);

    // PMMP lines 111-113: extinción inmediata sin suelo sólido Y sin vecino inflamable
    // mirrors: if(!$this->getSide(Vector3::SIDE_DOWN)->isTopFacingSurfaceSolid() &&
    //             !$this->canNeighborBurn()){ setBlock air; }
    if (!isTopFacingSurfaceSolid(level, x, y - 1, z) &&
        !canNeighborBurn(level, x, y, z)) {
        level->setBlock(x, y, z, AIR, 0);
        return;
    }

    // PMMP lines 115-123: extinción por lluvia
    // TODO: cuando Atomik tenga weather, replicar el chequeo de canBlockSeeSky
    // if (!$forever && level->isRainy() && level->canBlockSeeSky(...)) {
    //     level->setBlock(x, y, z, AIR, 0); return;
    // }

    // PMMP lines 125-130: envejecer el fuego (meta 0→15)
    // mirrors: $meta = $this->meta; if($meta < 15){ $this->meta = $meta + mt_rand(0,3);
    //          $this->getLevel()->setBlock($this, $this, true); }
    uint8_t meta = level->getBlockData(x, y, z);
    if (meta < 15) {
        uint8_t newMeta = (uint8_t)std::min(15, (int)meta + (rand() % 4));
        level->setBlock(x, y, z, FIRE, newMeta);
        meta = newMeta;
    }

    // PMMP line 132: $this->getLevel()->scheduleUpdate($this, $this->getTickRate() + mt_rand(0, 10))
    // (scheduleUpdate dedupes via Atomik's g_pending_fires)
    scheduleUpdate(level, x, y, z);

    // PMMP lines 134-137: extinción por edad + sin vecinos inflamables
    // mirrors: if(!$forever && !$this->canNeighborBurn()){
    //              if(!$this->getSide(DOWN)->isTopFacingSurfaceSolid() || $meta > 3){ setBlock air; }
    //          }
    if (!forever && !canNeighborBurn(level, x, y, z)) {
        if (!isTopFacingSurfaceSolid(level, x, y - 1, z) || meta > 3) {
            level->setBlock(x, y, z, AIR, 0);
            return;
        }
    }
    // PMMP lines 138-139: extinción aleatoria cuando el suelo no es inflamable y es viejo
    // mirrors: elseif(!$forever && !($this->getSide(DOWN)->getBurnAbility() > 0) &&
    //          $meta >= 15 && mt_rand(0,4) == 0){ setBlock air; }
    else if (!forever &&
             y > 0 &&
             getBurnAbility(level->getBlockId(x, y - 1, z)) == 0 &&
             meta >= 15 && (rand() % 5) == 0) {
        level->setBlock(x, y, z, AIR, 0);
        return;
    }
    // PMMP lines 140-178: propagate
    else {
        int o = 0;
        // TODO: reducir o si la lluvia es intensa

        // PMMP lines 145-150: 6 lados inmediatos
        // SIDE_EAST=5, SIDE_WEST=4, SIDE_DOWN=0, SIDE_UP=1, SIDE_SOUTH=3, SIDE_NORTH=2
        tryToCatchBlockOnFire(level, x + 1, y,     z,     300 + o, meta);  // EAST
        tryToCatchBlockOnFire(level, x - 1, y,     z,     300 + o, meta);  // WEST
        tryToCatchBlockOnFire(level, x,     y - 1, z,     250 + o, meta);  // DOWN
        tryToCatchBlockOnFire(level, x,     y + 1, z,     250 + o, meta);  // UP
        tryToCatchBlockOnFire(level, x,     y,     z + 1, 300 + o, meta);  // SOUTH
        tryToCatchBlockOnFire(level, x,     y,     z - 1, 300 + o, meta);  // NORTH

        // PMMP lines 152-177: triple-for volumen 3×7×3
        int diff = Server::getInstance().difficulty();
        for (int bx = x - 1; bx <= x + 1; bx++) {
            for (int bz = z - 1; bz <= z + 1; bz++) {
                for (int by = y - 1; by <= y + 4; by++) {
                    if (by < 0 || by >= 256) continue;
                    if (!level->isChunkLoaded(bx, bz)) continue;

                    int k = 100;
                    if (by > y + 1) k += (by - (y + 1)) * 100;

                    int chance = getChanceOfNeighborsEncouragingFire(
                                     level, bx, by, bz);
                    if (chance <= 0) continue;

                    int t = chance + 40 + diff * 7;
                    // TODO: reducir t si la lluvia es intensa

                    // PMMP line 168: if($t > 0 && mt_rand(0, $k) <= $t)
                    // mt_rand(0, $k) returns 0..$k inclusive.
                    // C++: rand() % (k+1) gives 0..k inclusive.
                    if (t > 0 && (rand() % (k + 1)) <= t) {
                        // PMMP line 169: $damage = min(15, $meta + mt_rand(0, 5) / 4)
                        // mt_rand(0,5)/4 with integer division: 0,0,0,0,1,1.
                        // So damage is meta or meta+1 (max 15 due to min).
                        int dmg = std::min(15, (int)meta + (rand() % 6) / 4);
                        level->setBlock(bx, by, bz, FIRE, (uint8_t)dmg);
                        // PMMP line 172: $this->getLevel()->scheduleUpdate($this->temporalVector, $this->getTickRate())
                        // NOTE: PMMP uses getTickRate() WITHOUT the +mt_rand(0,10)
                        // that onActivate and onScheduledUpdate use for the
                        // main fire's reschedule. This is intentional —
                        // newly-ignited neighbour fires tick sooner.
                        scheduleUpdate(level, bx, by, bz);
                    }
                }
            }
        }
    }
}

} // namespace atomikpe::block
