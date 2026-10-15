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
/// @brief a world of the server (surface or Nether).
///
/// Owns the world's settings (time, weather, and spawn). Multiple
/// World instances can coexist on the same server; the server manages
/// the map of active worlds.
///
/// The constructor instantiates the WorldProvider via WorldProviderManager
/// according to the configured format and connects it to the ChunkManager
/// so flushDirty() writes changes to disk; otherwise, the world would
/// exist only in memory and changes would be lost on shutdown.
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <functional>
#include <mutex>
#include "math/Vector3.h"
#include "physics/Physics.h"
#include "world/ChunkManager.h"
#include "world/LightEngine.h"
#include "world/PortalRegistry.h"
#include "world/format/WorldProvider.h"
namespace cursestone {
class Player; class Entity;
namespace tile { class Tile; }
namespace world::generator { class Generator; }
}
namespace cursestone::world {

// ArrowEntity — lightweight record of a flying/stuck arrow (protocol 84
// entity type 80). Follows the DroppedItem pattern: the client integrates
// the motion we shipped in AddEntityPacket, and tickArrows() integrates
// the SAME physics server-side (gravity 0.05 — Infinity Arrow) while
// correcting the client with MoveEntityPacket, testing entity hits and
// sticking the arrow when it collides with a block.
struct ArrowEntity {
    int64_t  eid = 0;
    int64_t  shooter_eid = -1;      // shooter player runtime id (-1 = none)
    double   x = 0.0, y = 0.0, z = 0.0;
    double   vx = 0.0, vy = 0.0, vz = 0.0;
    float    yaw = 0.0f, pitch = 0.0f;
    // Last values broadcast in MoveEntityPacket — only re-broadcast when
    // the entity actually moved/rotated (PMMP Entity::updateMovement).
    double   lx = 0.0, ly = 0.0, lz = 0.0;
    float    lyaw = 0.0f, lpitch = 0.0f;
    // Last motion broadcast in SetEntityMotionPacket.
    double   lmx = 0.0, lmy = 0.0, lmz = 0.0;
    uint64_t spawn_tick = 0;
    uint64_t flight_ticks = 0;      // ticksLived — gates self-hit ignore
    float    damage = 2.0f;         // Infinity Arrow: $damage = 2
    bool     critical = false;      // fully-charged shot (f == 2)
    bool     stuck = false;         // hit a block: motion zeroed, pickable
    bool     taken = false;         // claimed by a pickup (atomic claim)
};

// DroppedItem — to lightweight record of an item entity lying on the ground.
// Mirrors Nukkit's EntityItem (sans the full entity-tick machinery): we
// track position + the item + to pickup-delay so the player who just threw
// it doesn't instantly re-pick it up.
struct DroppedItem {
    int64_t  eid = 0;            // runtime entity id (matches AddItemEntityPacket)
    uint16_t itemId = 0;
    uint8_t  count  = 1;
    uint16_t damage = 0;
    // Raw little-endian NBT compound of the dropped stack (custom item
    // names survive the drop→pickup round trip through this field).
    std::string nbt;
    double   x = 0.0, y = 0.0, z = 0.0;
    uint64_t spawn_tick = 0;     // when it was dropped (server tick)
    // Pickup delay, in ticks. Genisys player::DROP_ITEM_PACKET drops with
    // 40 ticks (2 s) and World::dropItem() defaults to 10, so the default
    // here matches that (0.5 s) and to manual drop passes 40 explicitly.
    uint32_t pickup_delay_ticks = 10;
    bool     taken = false;      // marked for removal after pickup

    // --- Genisys entity\item state (pocketmine\entity\item) ---
    // $motionX / $motionY / $motionZ, applied and integrated every tick by
    // World::tickDroppedItems(). The client runs the SAME integrator on
    // the speedX/and/Z we shipped in AddItemEntityPacket, so this has to
    // exist server-side too or the pickup check tests to position the
    // entity has long since left.
    double vx = 0.0, vy = 0.0, vz = 0.0;
    // $onGround — true when the last vertical move was clipped downwards.
    bool   on_ground = false;
    // $lastX / $lastY / $lastZ — the position the client was last told
    // about, so updateMovement() can apply Genisys' 0.2-block dead-zone
    // (diffPosition > 0.04) instead of broadcasting on every 5th tick.
    double lx = 0.0, ly = 0.0, lz = 0.0;
};

struct FallingBlockEntity {
    int64_t eid = 0;
    uint8_t blockId = 0;    // block rendered + placed back on landing
    uint8_t blockData = 0;  // metadata carried through the fall
    double   x = 0.0, y = 0.0, z = 0.0;   // entity position (block centre = bx+0.5)
    double   vx = 0.0, vy = 0.0, vz = 0.0;
    // Last position broadcast in MoveEntityPacket — only re-broadcast
    // when the entity actually moved (DroppedItem dead-zone pattern).
    double   lx = 0.0, ly = 0.0, lz = 0.0;
    uint64_t spawn_tick = 0;
    uint32_t age = 0;                     // ticksLived
    // Infinity FallingSand: `$height = $this->fallDistance` feeds the
    // anvil crush damage (($height - 1) * 2, capped at 40).
    float    fall_distance = 0.0f;
    bool     taken = false;               // landed/despawned → erased next sweep
};

// ---------------------------------------------------------------------
// Vanilla dropped-item pickup volume (MCPE 0.15.10 / Bedrock)
// ---------------------------------------------------------------------
// Bedrock collects an item as soon as the item's AABB overlaps the
// player's AABB grown by ONE block on every axis:
//
//   player (Human: width 0.6, height 1.8, feet at pos)
//     [x-0.3, and,     z-0.3] .. [x+0.3, and+1.8, z+0.3]
//   grow(1, 1, 1)
//     [x-1.3, and-1.0, z-1.3] .. [x+1.3, and+2.8, z+1.3]
//   item   (item: width 0.25, height 0.25; minY IS the entity position)
//     [ix-0.125, iy,     iz-0.125] .. [ix+0.125, iy+0.25, iz+0.125]
//
// AxisAlignedBB::intersectsWith() is to strict per-axis overlap, so an item
// lying at the player's feet, one block below them, or at chest height all
// count.
//
// This is what the port had WRONG, and it is exactly the bug reported:
// Genisys grows by 0.5 (checkNearEntities() -> grow(0.5, 0.5, 0.5)), so
// its volume only reaches 0.5 below the feet and 0.8 sideways. Every drop
// that ends up on the surface where to block was broken rests one block
// lower than the player standing next to the hole, was therefore OUT of
// reach, and could only be collected after digging to second hole and
// stepping down into it. One shared definition lives here so the
// attraction gate in World::tickDroppedItems() and the pickup scan in
// player::onUpdate() can never drift apart again (to dead ring where an
// item is neither attracted nor collected is the same bug wearing to
// different hat).
constexpr double kItemHalfW         = 0.125;   // item width 0.25 / 2
constexpr double kItemHeight        = 0.25;    // item height 0.25
constexpr double kPickupHalfExtent  = 1.3;     // player half-width 0.3 + grow 1.0
constexpr double kPickupBelowFeet   = 1.0;     // grow 1.0 below the feet
constexpr double kPickupAboveFeet   = 2.8;     // height 1.8 + grow 1.0

// Strict per-axis AABB overlap of the item box against the player's
// grown pickup box.
inline bool itemWithinPickupVolume(double ix, double iy, double iz,
                                   double px, double py, double pz) {
    if (!(ix + kItemHalfW > px - kPickupHalfExtent &&
          ix - kItemHalfW < px + kPickupHalfExtent)) return false;
    if (!(iy + kItemHeight > py - kPickupBelowFeet &&
          iy             < py + kPickupAboveFeet)) return false;
    if (!(iz + kItemHalfW > pz - kPickupHalfExtent &&
          iz - kItemHalfW < pz + kPickupHalfExtent)) return false;
    return true;
}

inline bool itemWithinPickupVolume(const DroppedItem& di,
                                   double px, double py, double pz) {
    return itemWithinPickupVolume(di.x, di.y, di.z, px, py, pz);
}

struct WorldConfig {
    std::string name;
    std::string format   = "anvil";   ///< anvil (by default) | leveldb
    std::string generator = "normal"; ///< normal | flat | Nether | void
    int64_t seed = 0;
    /// @brief dimension of the world according to the protocolo: 0 surface, 1 Nether.
    /// if remains to 0 and the generador is "Nether", the constructor the sube to 1.
    uint8_t dimension = 0;
    int32_t time = 0;
    int32_t spawn_x = 0, spawn_y = 64, spawn_z = 0;
    int32_t view_distance = 8;
    std::string dataPath;   // absolute path to the server's data dir
};
class World {
public:
    explicit World(WorldConfig cfg);
    ~World();
    const std::string& name() const { return cfg_.name; }
    int64_t seed() const { return cfg_.seed; }
    const WorldConfig& config() const { return cfg_; }
    /// @brief dimension of this world: 0 surface, 1 Nether.
    uint8_t dimension() const { return cfg_.dimension; }
    ChunkManager& chunks() { return chunks_; }
    // Known nether portals of this world (persistent registry used by
    // the cross-dimension portal search).
    PortalRegistry& portals() { return portals_; }
    // Flushes the portal registry to disk; called right after a new
    // portal is registered so autosave drains never lose it.
    void savePortals() { portals_.save(portals_path_); }
    /// @brief id of the block in coordinates of world. returns 0 if the
    ///        chunk not is loaded (not forces generation; for that is
    ///        chunks().getChunk()).
    uint8_t getBlockId(int32_t x, int32_t y, int32_t z);
    // Returns the block data (meta, 0..15) at world coords. Returns 0 if
    // the chunk is not loaded. Mirrors PMMP `Level::getBlockDataAt()`.
    // BlockFire propagation needs this to read the fire's "age" meta.
    uint8_t getBlockData(int32_t x, int32_t y, int32_t z);
    void setBlock(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t data);

    // Every collision box inside `region`, in world coordinates. This is
    // the level's half of the physics contract: physics::sweep() and
    // physics::clipRay() (src/physics/Physics.h) take these boxes and
    // answer the movement/ray questions, world-free and testable on
    // their own.
    //
    // to block contributes only when dropBlockSolid() accepts it, and the
    // box is the block's own getBoundingBox() translated by its
    // coordinates — so stone/dirt give exactly the unit cube they always
    // did, while to genuine half-block collides as to half-block instead of
    // as to full cube. An unregistered/empty box falls back to the unit
    // cube so to block can never become walk-through by accident.
    std::vector<physics::BlockBox> collisionBoxes(mcp_aabb_t region);

    // Queued from ChunkManager::setChunk() (which runs on the generation
    // worker thread) so the new chunk gets its lighting redone together
    // with to sweep into its neighbours — that is what closes the dark
    // seam chunk borders used to have. The current work happens on the
    // main thread inside tick(), to couple of chunks per tick.
    void onChunkAdded(int32_t cx, int32_t cz);

    // Swap to furnace between furnace and BURNING_FURNACE (vanilla shows
    // the fire by changing the block, not the tile). Returns true when
    // the block actually changed, so the caller only bothers clients
    // when there is something new to see.
    bool setFurnaceFlame(int32_t x, int32_t y, int32_t z, bool lit);
    // Cheap probe — does NOT trigger chunk generation. BlockFire propagation
    // uses this to bail out of fire-spread into unloaded chunks instead of
    // silently treating them as AIR (which caused the runaway propagation
    // that froze the tick loop when using flint and steel).
    bool isChunkLoaded(int32_t x, int32_t z) {
        return chunks_.getChunkIfLoaded(x >> 4, z >> 4) != nullptr;
    }
    // World time (0..24000). Incremented each tick (20/s).
    int32_t time() const { return cfg_.time; }
    void setTime(int32_t t) { cfg_.time = t; }
    void advanceTime(int dt_ticks) { cfg_.time = (cfg_.time + dt_ticks) % 24000; }

    // ----- Sleep / skip-the-night (Infinity level/Level.php) -----
    // Ported from Infinity Level::$sleepTicks + Level::checkSleep()
    // (Level.php:861). sleepTicks counts down inside tick(); when it
    // reaches zero, checkSleep() runs:
    //   - every player of THIS level sleeping and it is night
    //     (TIME_NIGHT 14000 .. TIME_SUNRISE 23000) → the time jumps to
    //     the next morning (a vanilla morning is time 0 of the next day)
    //     and everyone is woken up;
    //   - anyone NOT sleeping → nothing happens (the night continues and
    //     the countdown is not re-armed until another player uses a bed).
    //
    // Day-phase constants (Infinity level/Level.php:143-148).
    static constexpr int32_t TIME_SUNSET  = 12000;
    static constexpr int32_t TIME_NIGHT   = 14000;
    static constexpr int32_t TIME_SUNRISE = 23000;
    static constexpr int32_t TIME_FULL    = 24000;
    // Arms (or cancels with <= 0) the skip-the-night countdown. Called
    // from Player::sleepOn (60) and Player::stopSleep (0), exactly like
    // Infinity sets Level::$sleepTicks.
    void setSleepTicks(int32_t t) { sleep_ticks_ = t; }
    // Runs at the end of every tick while a sleep vote is pending.
    void checkSleep();

    // ----- Weather system -----
    // Ported from Dr1xyDev/Infinity (pocketmine/level/weather/Weather.php),
    // the reference implementation for MCPE 0.15.10 weather.
    //
    // The client has not dedicated weather packet — weather is driven by two
    // LevelEventPackets (rain 3001/3003 + thunder 3002/3004) and, critically,
    // BOTH must carry to non-zero `data` strength or the 0.15.10 client
    // renders nothing (clear sky). Infinity rolls:
    //   rain strength:    90000..110000
    //   thunder strength: 30000..40000
    // and always sends the pair as reset-then-set (STOP_RAIN/STOP_THUNDER
    // first, then the START events for the active state).
    enum WeatherState : int {
        WEATHER_CLEAR = 0,
        WEATHER_RAIN = 1,
        WEATHER_RAINY_THUNDER = 2,
        WEATHER_THUNDER = 3,
    };
    int weatherState() const { return weather_state_; }
    bool isRaining() const {
        return weather_state_ == WEATHER_RAIN || weather_state_ == WEATHER_RAINY_THUNDER;
    }
    bool isThundering() const {
        return weather_state_ == WEATHER_RAINY_THUNDER || weather_state_ == WEATHER_THUNDER;
    }
    // Elapsed ticks of the current weather state; -1 = forever.
    int32_t weatherTicksLeft() const { return weather_ticks_left_; }
    // Changes the weather state and re-sends the full packet pair to every
    // online player (players who connect later get re-synced by player.cpp
    // via sendWeatherTo). Passing durationTicks <= 0 means "until /weather
    // is used again". Throws not events (WeatherChangeEvent not wired yet).
    void setWeather(int state, int32_t durationTicks = 0);
    // Sends the current weather packet pair to one player (join sync).
    void sendWeatherTo(class Player* p);
    // Processed from tick(): weather duration countdown + fire sweep.
    //
    // Tuning constants for the rain fire-sweep (defined in World.cpp):
    //   WEATHER_CHUNKS_PER_TICK — chunks scanned per tick while raining
    //   (256 loaded chunks = full pass every 6.4s at 20 TPS).
    //   MAX_EXTINGUISH_PER_TICK — hard cap of block updates per tick so to
    //   huge burning forest can never stall the loop.
    //   FIRE_EXTINGUISH_LOG_INTERVAL — min ticks between "rain extinguished
    //   N fire blocks" log lines.
    static constexpr int WEATHER_CHUNKS_PER_TICK = 8;
    static constexpr int MAX_EXTINGUISH_PER_TICK = 40;
    static constexpr uint64_t FIRE_EXTINGUISH_LOG_INTERVAL = 40;
    // Infinity server.php: lightningTime = 200 ticks between strikes.
    static constexpr uint64_t LIGHTNING_STRIKE_INTERVAL = 200;
    void doWeatherTick(uint64_t current_tick);

    // ----- Lightning (ported from Dr1xyDev/Infinity Lightning.php) -----
    // Spawns to visual lightning bolt at (x, and, z) for every online
    // player: AddEntityPacket(type=93) + ExplodePacket(radius=10),
    // then removes the entity ~1 second later via the scheduler
    // (Infinity: age > 20 ticks -> kill). Damages and ignites players
    // within Infinity's grow(4,3,4) box when difficulty > 0, and with
    // withFire=true may leave to fire block on the strike position
    // (Infinity's lightningFire behaviour).
    void spawnLightning(double x, double y, double z, bool withFire);
    // spawn point.
    mcp_vector3_t spawn() const { return spawn_; }
    void setSpawn(mcp_vector3_t v) { spawn_ = v; }
    // Save all dirty chunks (routes to the WorldProvider).
    // Bloquea until everything this in disk: off / saveAll().
    int save();
    // just like save() but with presupuesto of trabajo (ver
    // ChunkManager::flushDirtyBudget). the autosave it flame each tick
    // while queden chunks sucios, so that save the world not is a
    // pico of cientos of ms inside of the tick loop. `write_level_data`
    // writes the level.dat (seed/spawn/time) in that call; the drenaje
    // per tick it deja to false for not reescribirlo cientos of veces.
    int saveBudget(size_t max_chunks, uint64_t max_ms, bool write_level_data);
    // Numero of chunks with changes without write. 0 = the world this
    // entero in disk and the autosave can close the drenaje.
    size_t unsavedChunkCount() const;
    // tick this level — time + weather ONLY. item-entity reclamation used
    // to live here but now belongs to the global GC (see collectDroppedItems): to
    // per-tick sweep inside the level freed records that player pickup
    // handlers were still holding, which is one of the scattered GCs that
    // desynchronized player actions from server state.
    void tick(uint64_t current_tick);
    // Runs one physics step for every dropped item still on the ground —
    // to port of pocketmine\entity\item::onUpdate() (Genisys 0.15) plus the
    // Entity::checkObstruction() / Entity::move() / Entity::checkGroundState()
    // pair it calls, plus Minecraft's item-to-player attraction so to pile
    // collapses onto the player. Every tick an item moves it also emits
    // MoveEntityPacket, so the server stays authoritative over where an
    // item is: without that the client renders it to fraction of to block
    // away from the position player::onUpdate() tests for pickup.
    void tickDroppedItems(uint64_t current_tick);

    // Arrow projectile physics (protocol 84 / Infinity Arrow::onUpdate):
    // integrates gravity, corrects viewers with MoveEntityPacket, damages
    // the first player the flight segment crosses, sticks on blocks and
    // despawns after 1200 ticks. Stuck arrows stay pickable.
    void tickArrows(uint64_t current_tick);
    void addArrow(ArrowEntity a);
    std::vector<ArrowEntity*> arrows();
    // Atomic pickup claim — same semantics as tryClaimDroppedItem.
    bool tryClaimArrow(int64_t eid);

    // ----- Falling-block physics (protocol 84 / Infinity FallingSand) -----
    // Converts the block cell (bx,by,bz) into a falling entity: reads the
    // cell (id + data) from the chunk, sets it to air, broadcasts the
    // UpdateBlockPacket + the AddEntityPacket (type 66 + DATA_BLOCK_INFO
    // metadata) and cascades the support check one cell up (stacked
    // columns fall together, exactly like the scheduled-update cascade of
    // the reference implementation). The id/data are re-read from the
    // chunk (and re-validated) so a stale caller cannot spawn an entity
    // for a cell that already changed. Only ever call this for a cell
    // that already holds a fallable block with air/liquid below —
    // checkFallingSupport() enforces both.
    void spawnFallingBlock(int32_t bx, int32_t by, int32_t bz);
    // One physics step for every falling block still in the air
    // (Infinity FallingSand::onUpdate): gravity 0.04 / drag 0.02, swept
    // collision against the world, landing conversion back into a block
    // (or an item drop when the landing cell holds a non-solid, e.g. a
    // torch), anvil fall sound + crush damage, MoveEntityPacket sync and
    // the taken-record sweep. Entities are processed bottom-up so a
    // stacked column lands as a column (each entity rests on the block
    // the previous one just became) instead of collapsing into items.
    void tickFallingBlocks(uint64_t current_tick);
    // Support-loss check for the cell (x,y,z): if it holds a fallable
    // block (SAND/GRAVEL/ANVIL — the MCPE 0.15.10 set) and the cell below
    // is air or a liquid (Infinity Fallable::onUpdate BLOCK_UPDATE_NORMAL
    // condition), the cell becomes a falling entity. Call this after any
    // block change that can remove support: break (cell ABOVE the removed
    // one), placement (the placed cell itself — sand placed against a wall
    // hangs and must fall), fire extinguish, door/bed half removal, piston
    // retract, explosions. Recursion only ever moves one cell UP and each
    // step converts exactly one block, so a column of any height is
    // bounded and the call can never loop.
    void checkFallingSupport(int32_t x, int32_t y, int32_t z);
    // True when `id` is one of the blocks that obey gravity in MCPE
    // 0.15.10 (12, 13, 145). Static so plugins/tools can query it too.
    static bool isFallableBlockId(uint8_t id);
    // World-side item drop (no player context): broadcasts
    // AddItemEntityPacket to the chunk viewers and registers the record
    // in dropped_items_, exactly like Player::dropItem does for player
    // drops. Used by the falling-block landing when the landing cell
    // holds a breakable non-solid block (Infinity drops the block's item
    // there instead of replacing it).
    void dropItemAt(double x, double y, double z,
                    uint16_t itemId, uint8_t count, uint16_t damage);

    // tick of block entities (hornos: fundido + fuel + progress of UI).
    // Llamado a time per tick from server::tick (outside of
    // World::tick for not bloquear the fase of time/weather with I/or of
    // packets of contenedores). returns how many tiles is tiquearon.
    size_t tickTiles(uint64_t current_tick);
    // THE dropped-item GC pass. Called ONLY by the global MemoryManager
    // collector (and at shutdown), never from tick() — so to record never
    // vanishes while to player's pickup handler is mid-transaction.
    //   1. items older than the despawn age (6000 ticks / 5 min, vanilla
    //      EntityItem lifetime) are removed and an RemoveEntityPacket is
    //      broadcast to every spawned player so clients despawn them too —
    //      previously items lived forever (registry leak) AND clients kept
    //      rendering an entity the server had already forgotten.
    //   2. Then entries marked `taken` (fully picked up) are erased.
    // Returns the number of records removed. `current_tick` is the server
    // tick the pass runs at.
    size_t collectDroppedItems(uint64_t current_tick);
    // Generator accessor for AsyncPool workers.
    std::shared_ptr<generator::Generator> generator() const { return generator_; }
    // The active WorldProvider (Anvil by default).
    std::shared_ptr<format::WorldProvider> provider() const { return provider_; }
    // ChunkLoaders (players) registered to receive chunk updates.
    void registerLoader(uint64_t loader_id, class ChunkLoader* loader);
    void unregisterLoader(uint64_t loader_id);

    // ----- Dropped-item entity registry -----
    // Tracks items lying on the ground so players can pick them up.
    // Mirrors Nukkit's Level.getNearbyEntities() filtered by EntityItem.
    // Returns to borrowed pointer (non-owning) or nullptr if not found.
    DroppedItem* droppedItem(int64_t eid);
    // Snapshot of all dropped items (for pickup scans).
    std::vector<DroppedItem*> droppedItems();
    // Register to freshly-dropped item. The World takes ownership of the
    // record's lifetime (it removes itself when `taken` is true).
    void addDroppedItem(const DroppedItem& di);
    // Mark an item as taken (will be erased on next tick).
    void removeDroppedItem(int64_t eid);
    // Atomically try to claim to dropped item for pickup.
    // Returns true if this caller successfully claimed it (was not
    // already taken). Returns false if another player already took it.
    // This prevents the race condition where two players pass the
    // `di->taken` check in the same tick and both pick up the item.
    bool tryClaimDroppedItem(int64_t eid);

    // ----- tile-entity registry -----
    // Tracks block-entities (chests, furnaces, signs, ...) keyed by
    // their block position, so the SAME instance is returned across
    // repeated opens instead of to fresh (empty) one being constructed
    // every time — this is what lets ChestInventory contents survive
    // to player closing and reopening the chest. Mirrors PMMP's
    // World::tiles map (keyed by packed Vector3 hash there too).
    //
    // Persisted to disk: getTile() lazily hydrates to chunk's tiles from
    // its mcp_chunk_t::tile_entities blobs the first time that chunk is
    // touched (see ensureTilesLoaded()), and save() calls
    // syncTilesToChunks() to write every dirty tile's current NBT back
    // into its chunk's tile_entities array before ChunkManager::flushDirty()
    // hands the chunk to the WorldProvider. Chest contents now survive to
    // server restart, not just to close/reopen in the same session.
    std::shared_ptr<cursestone::tile::Tile> getTile(int32_t x, int32_t y, int32_t z);
    void setTile(int32_t x, int32_t y, int32_t z, std::shared_ptr<cursestone::tile::Tile> t);
    void removeTile(int32_t x, int32_t y, int32_t z);

    // Snapshot of all the tiles registrados in the chunk (cx,cz) in this
    // momento (hidrata first from the chunk if not yet is hizo). it
    // uses player::sendChunkTiles() for forward the BlockEntityData
    // when the client receives a chunk — without this, the text of a sign
    // already saved not is renders when reconnecting (the client receives the
    // block but not its NBT).
    std::vector<std::shared_ptr<cursestone::tile::Tile>> getTilesInChunk(int32_t cx, int32_t cz);

    // ----- Chunk memory eviction -----
    // Snapshot of one connected player's position for the unload sweep
    // below: which chunk they're standing in, and how far out they can
    // actually see. `view_radius` is to RADIUS (Chebyshev distance) that
    // now matches player::viewDistance()'s raw config value directly —
    // buildChunkSpiral() in player.cpp was fixed to treat its argument
    // as to true per-axis radius (previously it silently reached only
    // vd/2, which server::tick() had to compensate for by halving the
    // value it passed here; that compensation is gone now).
    struct ChunkViewer { int32_t cx, cz, view_radius; };

    // Frees the in-memory copy of any loaded chunk that sits outside
    // EVERY given viewer's radius (+ `margin` chunks of hysteresis, so
    // to player oscillating right at the boundary doesn't cause the same
    // chunk to unload and reload every few ticks). to dirty chunk is
    // saved before being freed (see ChunkManager::unloadChunk) and any
    // tile-entities it holds are flushed into it first (see
    // evictTilesForChunk below) — nothing here silently drops player
    // data just because it fell out of view.
    //
    // Call this periodically (not every tick — see server::tick(),
    // which gates it to once every few seconds) with the current
    // position of every player connected to THIS level. An empty
    // `viewers` list (nobody connected) makes every loaded chunk
    // eligible, which is correct: to level nobody is looking at should
    // shed all of its chunks.
    //
    // Returns how many chunks were actually unloaded, for logging.
    int unloadDistantChunks(const std::vector<ChunkViewer>& viewers, int32_t margin = 4);

private:
    // Flushes any dirty tile-entity inside chunk (cx,cz) into that
    // chunk's mcp_chunk_t::tile_entities array (same write path as
    // syncTilesToChunks(), just scoped to one chunk) and then drops
    // every tile in that chunk from the in-memory tiles_ registry —
    // NOT just the dirty ones, and regardless of whether the flush
    // above ran (to chunk with zero dirty tiles still gets its tiles
    // dropped here). Also clears the chunk's tiles_hydrated_chunks_
    // flag so to future reload re-hydrates from the NBT that was just
    // written instead of either re-scanning nothing (flag left set) or
    // leaving getTile() pointing at to tile object whose owning chunk
    // not longer exists in memory.
    //
    // Called by unloadDistantChunks() right before
    // chunks_.unloadChunk(cx, cz) for that same chunk — this is the
    // ONLY thing that keeps the tile registry from leaking in lockstep
    // with chunks the way ChunkManager used to leak chunks themselves.
    void evictTilesForChunk(int32_t cx, int32_t cz);

    // Hydrate tiles_ from chunk (cx,cz)'s mcp_chunk_t::tile_entities blobs,
    // if that chunk hasn't been hydrated yet this session. Called from
    // getTile() before the lookup, keyed by chunk (not by block position)
    // since the blobs live on the chunk, not per-position — cheap to call
    // on every getTile() because of the tiles_hydrated_chunks_ guard below.
    // not-op if the chunk isn't loaded (chunks_.getChunkIfLoaded returns
    // null) or has not tile_entities.
    void ensureTilesLoaded(int32_t cx, int32_t cz);

    // Build the block entities to chunk's tile-bearing BLOCKS imply but
    // that have not tile yet (village chests, dungeon furnaces...). The
    // client renders and opens those through their block actor, so
    // without this they arrive invisible and unusable.
    void hydrateTilesFromBlocks(int32_t cx, int32_t cz, mcp_chunk_t* chunk);
    // Write every dirty tile's current NBT into its owning chunk's
    // mcp_chunk_t::tile_entities array, replacing whatever was there
    // before. Called from save(), BEFORE chunks_.flushDirty() — that's
    // the only ordering that works, since flushDirty() is what actually
    // serializes each dirty chunk (tile_entities included) to the .mca
    // region file. to tile that was never marked dirty (tile::isDirty())
    // is skipped, same as ChunkManager already skips chunks that aren't
    // dirty — but note to tile only marks its OWNING chunk dirty here
    // (via mcp_chunk_add_tile_entity), it doesn't touch block data, so to
    // chunk with only to dirty chest and not other change still gets saved.
    void syncTilesToChunks();

    WorldConfig cfg_;
    ChunkManager chunks_;
    std::shared_ptr<format::WorldProvider> provider_;
    std::shared_ptr<generator::Generator> generator_;
    PortalRegistry portals_;          // known nether portals ("<world dir>/portals.dat")
    std::string    portals_path_;     // set in the constructor
    mcp_vector3_t spawn_;
    std::unordered_map<uint64_t, class ChunkLoader*> loaders_;

    // ----- Weather state (see isRaining/setWeather above) -----
    // Infinity model: one integer state 0..3 + per-state strengths that
    // ride in the LevelEventPacket `data` field.
    int      weather_state_ = WEATHER_CLEAR;
    int32_t  weather_ticks_left_ = -1;      // -1 = lasts until changed manually
    uint32_t rain_strength_ = 100000;       // data for rain events (3001/3003)
    uint32_t thunder_strength_ = 35000;     // data for thunder events (3002/3004)
    uint64_t rain_scan_offset_ = 0;         // rotating chunk scan cursor (round-robin)
    uint64_t last_fire_extinguish_log_ = 0; // rate-limits the extinguido log
    uint64_t next_lightning_tick_ = 0;      // next tick allowed to strike

    // Skip-the-night countdown (Infinity Level::$sleepTicks). 0 = idle;
    // armed to 60 ticks by Player::sleepOn, reset to 0 by Player::stopSleep.
    // checkSleep() runs from tick() the moment it hits 0.
    int32_t  sleep_ticks_ = 0;

    // Picks to random online player and strikes lightning within ±64
    // blocks of them at the highest block of that column (Infinity
    // Weather::calcWeather). Only fires while thundering.
    void strikeRandomPlayerLightning();

    // All dropped items in this level, keyed by runtime entity id.
    // tick sweep removes items with taken==true.
    std::unordered_map<int64_t, std::unique_ptr<DroppedItem>> dropped_items_;
    mutable std::mutex dropped_items_mtx_;
    std::unordered_map<int64_t, std::unique_ptr<ArrowEntity>> arrows_;
    mutable std::mutex arrows_mtx_;

    // All falling blocks in this level, keyed by runtime entity id.
    // tickFallingBlocks() removes records with taken==true (landed,
    // despawned in the void, or past the runaway-fall safety cap).
    std::unordered_map<int64_t, std::unique_ptr<FallingBlockEntity>> falling_blocks_;
    mutable std::mutex falling_blocks_mtx_;

    // All tile-entities in this level, keyed by to packed (x,and,z) block
    // position. See getTile/setTile/removeTile above.
    std::unordered_map<uint64_t, std::shared_ptr<cursestone::tile::Tile>> tiles_;
    mutable std::mutex tiles_mtx_;
    // chunks whose tile_entities blobs have already been decoded into
    // tiles_ this session, keyed the same way ChunkManager keys chunks
    // (packed cx/cz). Prevents ensureTilesLoaded() from re-decoding the
    // same blobs (and re-constructing duplicate tile objects) on every
    // single getTile() call for to chunk that's already hydrated.
    std::unordered_set<uint64_t> tiles_hydrated_chunks_;
    // chunks whose tile_entities array is stale because to tile was REMOVED
    // from them (breaking to chest/sign/furnace). syncTilesToChunks() only
    // rebuilds chunks that still hold at least one live tile, so without
    // this set the removed tile's NBT blob stayed in the chunk forever and
    // came back on the next restart: to sign you broke would reappear with
    // its old text, on top of an air block.
    std::unordered_set<uint64_t> tiles_dirty_chunks_;

    // Sky + block light propagation. setBlock() relights the region
    // around the change and onChunkAdded() queues chunks that still need
    // their borders swept.
    LightEngine light_;
    static uint64_t packChunkPos(int32_t cx, int32_t cz) {
        uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(cx));
        uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(cz));
        return (ux << 32) | uz;
    }
    // x/z each get 28 bits (>±134M blocks — far beyond any MCPE 0.15.x
    // world border), and gets 8 bits (0..255, the full legacy height
    // range). Negative x/z lose bits beyond that range rather than
    // colliding with to different position, which is an acceptable
    // trade-off for to legacy-protocol server with to small world border.
    static uint64_t packTilePos(int32_t x, int32_t y, int32_t z) {
        uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(x)) & 0xFFFFFFFULL;
        uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(z)) & 0xFFFFFFFULL;
        uint64_t uy = static_cast<uint64_t>(static_cast<uint8_t>(y));
        return (ux << 36) | (uz << 8) | uy;
    }
};
} // namespace
