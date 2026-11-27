/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */

// Represents a world and manages its settings, provider, chunks, and persistence.
#pragma once
#include <string>
#include <memory>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <utility>
#include "math/Vector3.h"
#include "physics/Physics.h"
#include "world/ChunkManager.h"
#include "world/LightEngine.h"
#include "world/PortalRegistry.h"
#include "world/format/WorldProvider.h"
#include "entity/Living.h"

namespace cursestone {
class Player;
class Entity;
namespace entity::ai {
class MobAIManager;
}
namespace block {
class LiquidFlow;
}
namespace tile {
class Tile;
}
namespace world::generator {
class Generator;
}
}

namespace cursestone::world {
namespace particle {
class FloatingTextParticle;
class Particle;
}

// Distance beyond a player's grown hitbox where an arrow's client-known
// velocity is frozen to zero. Clients dead-reckon projectiles between
// packet bursts; near a player that drift can carry the arrow model into
// the hitbox and render a phantom hurt flash on creative targets. The
// radius guarantees at least one full client step of clearance at the
// moment the freeze takes effect.
constexpr double kArrowClientFreezeDist = 5.0;

// Represents a flying or stuck arrow and tracks its server-side physics.
struct ArrowEntity {
    int64_t eid = 0;
    int64_t shooter_eid = -1; // Shooter runtime ID; -1 means none.
    double x = 0.0, y = 0.0, z = 0.0;
    double vx = 0.0, vy = 0.0, vz = 0.0;
    float yaw = 0.0f, pitch = 0.0f;
    // Last movement values broadcast to clients.
    double lx = 0.0, ly = 0.0, lz = 0.0;
    float lyaw = 0.0f, lpitch = 0.0f;
    // Last motion values broadcast to clients.
    double lmx = 0.0, lmy = 0.0, lmz = 0.0;
    uint64_t spawn_tick = 0;
    uint64_t flight_ticks = 0; // Used to prevent early self-hits.
    float damage = 2.0f;       // Default the era reference Arrow damage.
    bool critical = false;     // True for fully charged shots.
    bool stuck = false;        // True after hitting a block.
    bool taken = false;        // True after being claimed for pickup.
    bool client_hold = false;  // Near a player: client velocity frozen to zero.
};

// Server-side record of a thrown splash potion (entity type 86).
// Physics mirror the vanilla 0.15.10 ThrownPotion binary: gravity 0.05,
// drag 0.99 (0.8 in water), throw power 0.5, up-angle offset -20.
struct SplashPotionEntity {
    int64_t eid = 0;
    int64_t shooter_eid = -1; // Thrower runtime ID; -1 means none.
    double x = 0.0, y = 0.0, z = 0.0;
    double vx = 0.0, vy = 0.0, vz = 0.0;
    uint8_t meta = 0;        // Potion data value.
    uint64_t spawn_tick = 0; // Server tick when thrown.
    bool taken = false;      // True after the splash consumed it.
};

// Server-side record of a thrown experience bottle (entity type 68).
// Same Throwable physics as the splash potion (gravity 0.05, drag 0.99);
// the break spawns 3..10 XP split into canonical orbs.
struct ExperienceBottleEntity {
    int64_t eid = 0;
    int64_t shooter_eid = -1;
    double x = 0.0, y = 0.0, z = 0.0;
    double vx = 0.0, vy = 0.0, vz = 0.0;
    uint64_t spawn_tick = 0;
    bool taken = false;
};

// Server-side record of an experience orb (entity type 69). Physics and
// behaviour mirror the vanilla ExperienceOrb binary: gravity -0.04, water
// buoyancy +0.2, horizontal drag 0.98 x block friction (0.588 default),
// lifetime 6000 ticks, value carried in metadata index 11 (TYPE_INT).
struct ExperienceOrbEntity {
    int64_t eid = 0;
    double x = 0.0, y = 0.0, z = 0.0;
    double vx = 0.0, vy = 0.0, vz = 0.0;
    float yaw = 0.0f;            // Spawn heading (vanilla draws it randomly).
    int value = 1;               // XP points (metadata index 11).
    uint64_t spawn_tick = 0;
    uint32_t age = 0;            // Ticks alive; kill at 6000.
    int64_t follow_eid = 0;      // Player the orb is attracted to (unique id).
    uint32_t follow_tick = 0;    // Age tick when the player was acquired.
    bool taken = false;          // True after pickup / expiry.
};

// Tracks a dropped item, its position, motion, and pickup state.
struct DroppedItem {
    int64_t eid = 0; // Runtime entity ID.
    uint16_t itemId = 0;
    uint8_t count = 1;
    uint16_t damage = 0;
    // Raw little-endian NBT preserves custom item names.
    std::string nbt;
    double x = 0.0, y = 0.0, z = 0.0;
    uint64_t spawn_tick = 0; // Server tick when dropped.
    // Pickup delay in ticks; defaults to 10 (0.5 seconds).
    uint32_t pickup_delay_ticks = 10;
    bool taken = false; // Marked for removal after pickup.

    // Motion is integrated server-side to match the client.
    double vx = 0.0, vy = 0.0, vz = 0.0;
    // True when the last downward movement was blocked.
    bool on_ground = false;
    // Last position sent to clients for movement updates.
    double lx = 0.0, ly = 0.0, lz = 0.0;
    // Last motion sent to clients, so SetEntityMotionPacket only goes out
    // when the velocity actually drifted.
    double lmx = 0.0, lmy = 0.0, lmz = 0.0;
};

struct FallingBlockEntity {
    int64_t eid = 0;
    uint8_t blockId = 0;              // Block ID restored on landing.
    uint8_t blockData = 0;            // Block metadata preserved during the fall.
    double x = 0.0, y = 0.0, z = 0.0; // Entity position.
    double vx = 0.0, vy = 0.0, vz = 0.0;
    // Last position broadcast to clients.
    double lx = 0.0, ly = 0.0, lz = 0.0;
    uint64_t spawn_tick = 0;
    uint32_t age = 0; // Ticks lived.
    // Used to calculate anvil fall damage.
    float fall_distance = 0.0f;
    bool taken = false; // Removed on the next cleanup pass.
};

// Server-side record of a mob. The struct carries only identity,
// position and the chunk index: all per-mob dynamic state (AI goals,
// intent, physics) lives in entity/ai/'s MobBrain, keyed by eid. The
// chunk index (cx/cz) mirrors mobs_by_chunk_ so chunk streams touch
// O(mobs of the chunk) instead of scanning the whole world map.
//
// Sentinel for spawnMob/moveMob head-yaw parameters: "no explicit head
// facing" — the head rides the body yaw, which is the contract the plugin
// bindings (world.moveEntity / native moveMob) were written against. The
// AI actuation passes a real head yaw; plugins keep their old behavior.
constexpr float kHeadYawFollowBody = -1000.0f;

// Combat state lives here (not in the brain): damage can arrive from any
// thread through the plugin APIs, while brains are main-thread-only.
struct MobEntity {
    int64_t eid = 0;
    int32_t type = 0;  // Protocol-84 network id (entity::EntityTypes).
    double x = 0.0, y = 0.0, z = 0.0; // Feet position.
    float yaw = 0.0f, pitch = 0.0f;
    // The head's own facing. The 0.15.10 client renders the body from
    // the move packet's yaw and the head from its headYaw byte, so the
    // two are independent channels: body yaw tracks the movement
    // direction while head yaw tracks the look target — a mob whose
    // body and head share one yaw reads as moonwalking (facing the
    // player it watches while sliding sideways toward its wander
    // target).
    float head_yaw = 0.0f;
    int32_t cx = 0, cz = 0; // Chunk the entity is indexed under.
    // Vanilla-shaped health, seeded from entity::mobMaxHealth(type) on spawn.
    float health = 20.0f;
    float max_health = 20.0f;
    // Pending knockback (blocks/tick) written by the damage pipeline and
    // consumed by the next AI actuation pass; kb_tick is the server tick
    // the shove was applied, so a stale field expires instead of firing
    // after a long gate closure.
    float kbx = 0.0f, kbz = 0.0f;
    uint64_t kb_tick = 0;
    // Panic reflex (the same cross-thread contract as the knockback):
    // every hit stamps the away-from-damager direction and the tick it
    // landed. The AI consumes the reflex on its next pass — passive
    // mobs bolt, the sprint pose rides the FLAGS metadata byte — and
    // the reflex expires when the vanilla panic window closes.
    float panic_dir_x = 0.0f, panic_dir_z = 0.0f;
    uint64_t panic_tick = 0;
    // Last attacker stamp (same cross-thread contract): the runtime id
    // of the entity that landed the most recent hit plus the tick it
    // landed. The 0.8.1-era target goals read it — HurtByTargetGoal
    // retaliates against the attacker and PanicGoal flees while the
    // stamp is fresh. last_hurt_by_player disambiguates a player eid
    // from a mob eid on lookup.
    int64_t last_hurt_by_eid = 0;
    bool last_hurt_by_player = false;
    uint64_t last_hurt_tick = 0;
    // Death window: non-zero once the lethal blow landed, holding the
    // server tick the record must be removed at (death animation plays
    // for ~one second first). Zero means alive.
    uint64_t death_tick = 0;
};

// Defines the vanilla dropped-item pickup volume for MCPE 0.15.10.
// Items are collected when their bounding boxes overlap the player's pickup
// volume: the player box grown by 0.5 on every axis, exactly the scan the
// 0.15-era reference runs in Player::checkNearEntities
// (boundingBox->grow(0.5, 0.5, 0.5)). The player box is 0.6 wide (half
// width 0.3) and 1.8 tall, so the volume reaches 0.8 blocks horizontally,
// 0.5 below the feet and 2.3 above them. The shared bounds keep the item
// scan and the arrow scan consistent.
constexpr double kItemHalfW = 0.125;      // Half of the item width.
constexpr double kItemHeight = 0.25;      // Item height.
constexpr double kPickupHalfExtent = 0.8; // Player half-width (0.3) + grow (0.5).
constexpr double kPickupBelowFeet = 0.5;  // grow (0.5).
constexpr double kPickupAboveFeet = 2.3;  // Player height (1.8) + grow (0.5).

// The 0.15-era pickup volume is the player box grown by 0.5 on every axis;
// pin the derived constants so a future edit cannot silently widen the
// volume back past what the reference scan reaches.
static_assert(kPickupHalfExtent == 0.8, "pickup half-extent must stay at 0.3 + 0.5");
static_assert(kPickupBelowFeet == 0.5, "pickup reach below feet must stay at 0.5");
static_assert(kPickupAboveFeet == 2.3, "pickup reach above feet must stay at 1.8 + 0.5");

// Checks strict AABB overlap between an item and the player's pickup volume.
inline bool itemWithinPickupVolume(double ix, double iy, double iz, double px, double py,
                                   double pz) {
    if (!(ix + kItemHalfW > px - kPickupHalfExtent && ix - kItemHalfW < px + kPickupHalfExtent))
        return false;
    if (!(iy + kItemHeight > py - kPickupBelowFeet && iy < py + kPickupAboveFeet))
        return false;
    if (!(iz + kItemHalfW > pz - kPickupHalfExtent && iz - kItemHalfW < pz + kPickupHalfExtent))
        return false;
    return true;
}

inline bool itemWithinPickupVolume(const DroppedItem& di, double px, double py, double pz) {
    return itemWithinPickupVolume(di.x, di.y, di.z, px, py, pz);
}

struct WorldConfig {
    std::string name;
    std::string format = "anvil";     ///< Supported formats: anvil and leveldb.
    std::string generator = "normal"; ///< Supported generators: normal, flat, Nether, void.
    int64_t seed = 0;
    /// @brief World dimension: 0 for surface, 1 for Nether.
    /// Nether generators default to dimension 1 when unset.
    uint8_t dimension = 0;
    int32_t time = 0;
    int32_t spawn_x = 0, spawn_y = 64, spawn_z = 0;
    int32_t view_distance = 8;
    /// @brief Ceiling of the mob record map (the old compile-time
    ///        kMaxMobs, configurable through the max-mobs property so
    ///        large-population deployments can raise it; records only
    ///        exist for resident chunks, so the ceiling bounds worst-case
    ///        memory, not steady-state cost).
    size_t max_mobs = 1000000;
    std::string dataPath; // Absolute server data directory.
};

class World {
  public:
    explicit World(WorldConfig cfg);
    ~World();

    const std::string& name() const {
        return cfg_.name;
    }

    int64_t seed() const {
        return cfg_.seed;
    }

    const WorldConfig& config() const {
        return cfg_;
    }

    /// @brief Returns the world dimension: 0 for surface, 1 for Nether.
    uint8_t dimension() const {
        return cfg_.dimension;
    }

    ChunkManager& chunks() {
        return chunks_;
    }

    // Provides access to this world's persistent Nether portal registry.
    PortalRegistry& portals() {
        return portals_;
    }

    // Saves the portal registry immediately after registration.
    void savePortals() {
        portals_.save(portals_path_);
    }

    /// @brief Returns the block ID; unloaded chunks are not generated.
    uint8_t getBlockId(int32_t x, int32_t y, int32_t z);

    // Returns block metadata, or 0 when the chunk is unloaded.
    uint8_t getBlockData(int32_t x, int32_t y, int32_t z);

    // notifyLiquid=false is for the liquid engine's own writes (vanilla
    // UPDATE_CLIENTS): skips the liquid neighbor wake-up to avoid re-entry.
    void setBlock(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t data,
                  bool notifyLiquid = true);

    // Returns world-space collision boxes within the requested region.
    std::vector<physics::BlockBox> collisionBoxes(mcp_aabb_t region);

    // Same collection as collisionBoxes(region) but fills a caller-owned
    // vector (cleared first): the mob AI actuation pass runs this once
    // per moving mob per tick, so the per-call allocation of the
    // returning overload would churn the heap for nothing.
    void collisionBoxes(mcp_aabb_t region, std::vector<physics::BlockBox>& out);

    // Whether the block id carries an entity collision volume. The
    // movement collision cubes run on this same table, so block
    // placement refuses exactly the cells an entity could not share.
    static bool entityCollidable(uint8_t id);

    // Queues lighting updates for newly added chunks. `full` selects the
    // entry kind: true = blocks changed under the chunk (spill batches),
    // false = the chunk merely arrived with its worker-lit interior (a
    // cheap border pass instead of the 46x46 re-flood).
    void onChunkAdded(int32_t cx, int32_t cz, bool full = true);

    // Thin main-thread accessors over the light queue, used by the
    // /status diagnostics and the perf tools (tools/light_bench.cpp):
    // same contract as LightEngine::processPending / hasPending.
    size_t drainLightPending(int32_t budget, int32_t max_ms = 0) {
        return light_.processPending(budget, max_ms);
    }
    bool hasPendingLight() const {
        return light_.hasPending();
    }

    // Run the queued light entry for exactly this chunk right now, before
    // its FullChunkDataPacket is serialized. Protocol 84 clients keep the
    // light bytes they receive — a chunk that leaves before its border
    // pass would freeze the seam into the client until it re-streams.
    // Returns false when nothing was queued (already finalized).
    bool flushLightForChunk(int32_t cx, int32_t cz) {
        return light_.flushPending(cx, cz);
    }

    // Direct engine access for the tools: tools/light_oracle.cpp drives
    // the ground-truth relightBoxExact through it. Not for gameplay code
    // — use the queue accessors above.
    LightEngine& light() {
        return light_;
    }

    // Queues a dynamic-liquid scan for a chunk that just entered the
    // level (queueLiquidWake) and drains it on the main-thread tick
    // (processLiquidWakes). Only cells left DYNAMIC (flowing ids 8/10) are
    // resumed: a player flow interrupted by a save, a structure pool, a
    // spring cell — one vanilla tick lets them finish or settle and the
    // result persists. Still liquid (oceans, rivers, lakes, lava falls) is
    // generated settled and never re-enters the simulation on its own; a
    // static block starts flowing only when a NEIGHBOUR BLOCK CHANGES
    // (World::setBlock → LiquidFlow::onBlockChanged), which is exactly the
    // reference behaviour. The drain hands out only a few scheduled ticks
    // per tick and remembers a per-chunk scan cursor, so a freshly loading
    // world feeds the shared scheduler gradually instead of queueing one
    // callback per liquid cell in a single burst.
    void queueLiquidWake(int32_t cx, int32_t cz);
    void processLiquidWakes(int32_t budget);

    // Updates the furnace's lit state and reports whether it changed.
    bool setFurnaceFlame(int32_t x, int32_t y, int32_t z, bool lit);

    // Checks chunk loading without triggering generation.
    //
    // BLOCK coordinates: (x, z) are world cell positions and the owning
    // chunk is derived here (x >> 4). Chunk-coordinate residency probes
    // go through chunks().getChunkIfLoaded(cx, cz) instead — passing
    // chunk coords here shifts them a second time and silently probes
    // the wrong chunk (this froze the mob AI past 256 blocks of the
    // world origin once; see MobAIManager::actuateOne).
    bool isChunkLoaded(int32_t x, int32_t z) {
        return chunks_.getChunkIfLoaded(x >> 4, z >> 4) != nullptr;
    }

    // Returns the world time in ticks.
    int32_t time() const {
        return cfg_.time;
    }

    void setTime(int32_t t) {
        cfg_.time = t;
    }

    void advanceTime(int dt_ticks) {
        cfg_.time = (cfg_.time + dt_ticks) % 24000;
    }

    // ----- Sleep and skip-night system -----
    // Advances the pending sleep countdown and checks whether all players are sleeping.
    static constexpr int32_t TIME_SUNSET = 12000;
    static constexpr int32_t TIME_NIGHT = 14000;
    static constexpr int32_t TIME_SUNRISE = 23000;
    static constexpr int32_t TIME_FULL = 24000;

    // Period between server-side time re-anchors (protocol 84 SetTime
    // broadcast): 200 ticks = 10 s. The 0.15.10 client binds its clock to
    // the LAST SetTime it processed, so one lost packet — the login-time
    // one lands before the client world exists, plus plain network drops —
    // would otherwise leave the client sky stuck at its day default. The
    // periodic re-anchor keeps every client glued to server truth and
    // heals a dropped packet within ten seconds; one small packet per
    // player per interval.
    static constexpr uint32_t TIME_RESYNC_INTERVAL = 200;

    // Sends one SetTime re-anchor to every player of this world. Cheap:
    // returns without touching the wire when nobody is playing here.
    void broadcastTimeSync();

    // ----- Block construction API (WorldEdit-style plugins) -----
    // Sends the CURRENT id/data of one block to every viewer that can see
    // its chunk (one shared UpdateBlockPacket, FLAG_ALL_PRIORITY — the
    // same refresh the player-driven placement path sends). No-op when
    // the chunk is not resident or nobody is looking: never allocates.
    void broadcastBlockUpdate(int32_t x, int32_t y, int32_t z);

    // Vanilla-shaped /fill bulk write for building tools. Normalizes the
    // two corners, refuses volumes above the vanilla cap (32768 cells)
    // with -1, forces every affected chunk resident once, then writes the
    // id/data across the box (no-op cells — same id AND data — are
    // skipped). Viewers are refreshed with whole-chunk re-sends for the
    // heavily modified chunks and per-block packets for the lightly
    // touched ones: one FullChunkDataPacket carries 4096 cells, so for a
    // dense fill it replaces four thousand UpdateBlockPackets per viewer.
    // Returns the number of cells actually written (changed), or -1 when
    // the volume exceeds the cap.
    int32_t fillBlocks(int32_t x1, int32_t y1, int32_t z1, int32_t x2, int32_t y2,
                       int32_t z2, uint8_t id, uint8_t data);

    // Volume cap of fillBlocks (the vanilla /fill limit).
    static constexpr int32_t FILL_MAX_VOLUME = 32768;

    // One cell of a bulk cell-list write (setBlocks): position plus the
    // id/data to leave there.
    struct BlockWrite {
        int32_t x, y, z;
        uint8_t id, data;
    };

    // Bulk cell-list write for building tools (undo, copy/paste, brush
    // stamps): one call writes an arbitrary set of cells. Touched chunks
    // are made resident once, no-op cells (same id AND data) are skipped,
    // later entries override earlier ones for the same cell, and every
    // written cell meets its neighbors like a normal placement. Viewers
    // refresh through the same mixed path as fillBlocks (whole-chunk
    // re-sends for dense chunks, shared per-block packets for sparse
    // ones). Returns the number of cells actually written, or -1 when
    // count exceeds the cap.
    int32_t setBlocks(const BlockWrite* writes, int32_t count);

    // Bulk region read for building tools (undo snapshots): fills `out`
    // with (id, data) byte pairs, row-major in the same y -> x -> z order
    // fillBlocks iterates. Cold chunks are made resident first, so a
    // snapshot taken before a fill can never read phantom air. Returns
    // the cell count, or -1 when the region exceeds the cap (out is left
    // untouched then).
    int32_t readBlockRegion(int32_t x1, int32_t y1, int32_t z1, int32_t x2, int32_t y2,
                            int32_t z2, std::vector<uint8_t>& out);

    // Sets the skip-night countdown; non-positive values cancel it.
    void setSleepTicks(int32_t t) {
        sleep_ticks_ = t;
    }

    // Checks whether the pending sleep vote can skip the night.
    void checkSleep();

    // ----- Weather system -----
    // Uses MCPE 0.15.10 rain and thunder events with non-zero strengths.
    enum WeatherState : int {
        WEATHER_CLEAR = 0,
        WEATHER_RAIN = 1,
        WEATHER_RAINY_THUNDER = 2,
        WEATHER_THUNDER = 3,
    };

    int weatherState() const {
        return weather_state_;
    }

    bool isRaining() const {
        return weather_state_ == WEATHER_RAIN || weather_state_ == WEATHER_RAINY_THUNDER;
    }

    bool isThundering() const {
        return weather_state_ == WEATHER_RAINY_THUNDER || weather_state_ == WEATHER_THUNDER;
    }

    // Returns remaining weather ticks; -1 means unlimited duration.
    int32_t weatherTicksLeft() const {
        return weather_ticks_left_;
    }

    // Changes the weather and broadcasts its packet pair to online players.
    void setWeather(int state, int32_t durationTicks = 0);

    // Sends the current weather state to a joining player.
    void sendWeatherTo(class Player* p);

    // Updates weather duration and processes rain-related fire extinguishing.
    static constexpr int WEATHER_CHUNKS_PER_TICK = 8;
    static constexpr int MAX_EXTINGUISH_PER_TICK = 40;
    static constexpr uint64_t FIRE_EXTINGUISH_LOG_INTERVAL = 40;
    static constexpr uint64_t LIGHTNING_STRIKE_INTERVAL = 200;
    void doWeatherTick(uint64_t current_tick);

    // ----- Lightning system -----
    // Spawns lightning, applies damage, and optionally ignites the strike location.
    void spawnLightning(double x, double y, double z, bool withFire);

    // Returns the world spawn position.
    mcp_vector3_t spawn() const {
        return spawn_;
    }

    void setSpawn(mcp_vector3_t v) {
        spawn_ = v;
    }

    // Saves all dirty chunks and world data.
    int save();

    // Saves a limited number of chunks within a time budget. The
    // write_level_data pass (shutdown, /save, world unload and the
    // autosave's level-dat step) is a full save point: mob rosters ride
    // the same flush into the region files.
    int saveBudget(size_t max_chunks, uint64_t max_ms, bool write_level_data);

    // Rebuilds the entity list of every chunk whose mobs changed since
    // the last save pass (the per-chunk dirty set), so mob state rides
    // the chunk save path into the region files exactly like blocks and
    // tile entities. Called from every full save point.
    void syncMobsToChunks();

    // Flushes one chunk's alive mob records into its entity list and
    // drops the records: mob state lives and dies with its chunk, so an
    // evicted chunk carries its bodies on disk and a returning one
    // re-creates them from there. Runs while the chunk is still resident
    // (before ChunkManager::unloadChunk), mirroring evictTilesForChunk.
    void evictMobsForChunk(int32_t cx, int32_t cz);

    // Returns the number of chunks with unsaved changes.
    size_t unsavedChunkCount() const;

    // Advances world time and weather; entity cleanup is handled globally.
    void tick(uint64_t current_tick);

    /// @brief Starts a new tick sample and updates the moving average.
    /// @note Main thread only.
    void beginTickSample(double ms) {
        avgTickMs_ = avgTickMs_ * 0.95 + lastTickMs_ * 0.05;
        lastTickMs_ = ms;
    }

    /// @brief Adds time spent in a later tick phase.
    /// @note Main thread only.
    void addTickSample(double ms) {
        lastTickMs_ += ms;
    }

    /// @brief Returns the duration of the last completed world tick.
    double lastTickMs() const {
        return lastTickMs_;
    }

    // EMA of the per-tick light-drain cost (LightEngine::processPending),
    // the number the /status world line shows next to the tick average:
    // it is the component that spikes while a view of chunks generates.
    double avgLightMs() const {
        return avgLightMs_;
    }

    /// @brief Returns the moving average of world tick duration.
    double avgTickMs() const {
        return avgTickMs_;
    }

    // Updates dropped-item physics, movement packets, and player attraction.
    void tickDroppedItems(uint64_t current_tick);

    // Updates arrow physics, player hits, block collisions, and despawning.
    void tickArrows(uint64_t current_tick);
    void addArrow(ArrowEntity a);
    std::vector<ArrowEntity*> arrows();

    // Splash potion flight, block/player impact, and area effects.
    void tickSplashPotions(uint64_t current_tick);
    void addSplashPotion(SplashPotionEntity p);

    // --- Vanilla experience entities (0.15.10 binary) ---
    // Thrown XP bottle (entity type 68): Throwable physics, break spawns
    // 3..10 XP split into canonical orbs.
    void tickExperienceBottles(uint64_t current_tick);
    void addExperienceBottle(ExperienceBottleEntity p);
    // XP orbs (entity type 69): splits @p total into the canonical ladder
    // {1,3,7,17,37,73,149,307,617,1237,2477} the binary uses and spawns one
    // orb record + AddEntityPacket per piece. Values < 1 are ignored.
    void spawnXPOrbs(double x, double y, double z, int total);
    void tickExperienceOrbs(uint64_t current_tick);
    void addExperienceOrb(ExperienceOrbEntity p);

    // Per-mob active effects (splash potions, /effect). Sparse by design:
    // a mob without effects costs nothing but one empty-map check per tick.
    // Storage lives here instead of inside MobEntity so the hot mob struct
    // keeps its layout and the effects stay behind their own mutex.
    void addMobEffect(int64_t eid, std::shared_ptr<entity::Effect> e);
    void tickMobEffects(uint64_t current_tick);
    // Combined Speed/Slowness movement factor for a mob's real motion
    // (the vanilla Mob::_addSpeedBonus attribute-modifier behaviour):
    // x(1 + 0.2*(amp+1)) per Speed level and x max(0, 1 - 0.15*(amp+1))
    // per Slowness level, 1.0 when the mob has neither.
    float mobSpeedMultiplier(int64_t eid);

    // Atomically claims an arrow for pickup.
    bool tryClaimArrow(int64_t eid);

    // Broadcasts a particle to players who can see its chunk.
    void addParticle(const particle::Particle& p);

    // Spawns one protocol-84 particle by raw type id (1..32, see
    // particle::ParticleId) at a world position. Returns false for ids the
    // client has no renderer for, and for payloads the particle family
    // rejects (each particle file owns its packing; item break of an
    // unknown item or terrain of air would crash the client, never reach
    // the wire).
    bool spawnParticle(int32_t type, float x, float y, float z, int32_t data);

    // Broadcasts the protocol-84 splash-potion shatter (EVENT_PARTICLE_SPLASH):
    // one packet carries the colored swirl AND the glass smash sound.
    // @p rgb is 0xRRGGBB; the client receives it as opaque ARGB 0xFF______.
    bool spawnSplashParticles(float x, float y, float z, uint32_t rgb);

    // Broadcasts the protocol-84 break-particle event (EVENT_PARTICLE_DESTROY)
    // at a block's center with that block's texture. On protocol 84 this one
    // event carries BOTH the particle burst and the dig sound — the same
    // effect block breaking sends — so every "ground dust" style effect
    // (hard landings, sprint steps) reuses it instead of inventing events
    // the 0.15.10 client silently ignores.
    void broadcastBlockParticles(int32_t bx, int32_t by, int32_t bz,
                                 uint8_t blockId, uint8_t blockData);

    // ----- Floating text (particle/FloatingTextParticle) -----
    // Protocol 84 has no text-rendering particle and its client draws
    // nametags only on player entities, so floating text is the
    // FloatingTextParticle packet composite: ONE invisible client-side
    // fake player whose nametag is the whole text. The carrier spawns
    // through AddPlayerPacket + SetEntityDataPacket (invisible flag,
    // nametag, NoAI) and leaves through one RemoveEntityPacket; the
    // Genisys-style title line and every '\n' body line ride on that
    // single nametag and the client stacks the lines itself, so a text
    // costs one entity and one fixed spawn pair no matter how many lines
    // it carries.
    //
    // Five tag shapes share this one carrier:
    //   static    fixed anchor (the default);
    //   follow    anchors over one entity/player, repositioned by the
    //             budgeted follow pass past a movement threshold;
    //   filtered  only an allow list of player names renders it;
    //   hidden    exists but despawned until shown (visibility toggle);
    //   expiring  despawns after a TTL via the server scheduler.
    //
    // The per-world cap (kMaxFloatingTexts) plus the shared cached
    // packets keep the client-side nametag rendering bounded: spawn,
    // move, re-text and removal are the only packet moments, viewer
    // churn reconciles once per second via tickFloatingTexts(), the
    // follow pass touches a text only when its target moved past the
    // threshold, and a world without floating texts pays nothing.
    //
    // @p text may contain '\n' to split into title + body lines (the
    // Genisys tag pair). Returns the entity id (the carrier), or -1 when
    // the per-world cap is reached. @p ttlTicks > 0 despawns
    // automatically after that many ticks; 0 keeps it until
    // removeFloatingText() (ids do not survive restarts).
    int64_t spawnFloatingText(double x, double y, double z, std::string text,
                              int32_t ttlTicks);
    // Repositions a floating text's carrier for its current viewers.
    // Unknown ids fail.
    bool moveFloatingText(int64_t id, double x, double y, double z);
    // Replaces a floating text's content (same '\n' multi-line model:
    // the first line is the title, the rest the body); the change
    // reaches the wire as one SetEntityDataPacket. Unknown ids fail.
    bool setFloatingText(int64_t id, std::string text);
    // Genisys-style single-field re-texts; each costs at most one
    // SetEntityDataPacket. Unknown ids fail.
    bool setFloatingTextTitle(int64_t id, std::string title);
    bool setFloatingTextBody(int64_t id, std::string body);
    // Visibility toggle: hide despawns the text from every viewer, show
    // lets the next viewer sync re-arm the audience. Unknown ids fail.
    bool setFloatingTextVisible(int64_t id, bool visible);
    // Follow mode: anchors the text over one entity or player (0 = back
    // to the fixed spot). Unknown text ids fail.
    bool setFloatingTextFollow(int64_t id, int64_t targetEid);
    // Follow mode resolved by player name at call time. Fails for
    // unknown text ids or offline players.
    bool setFloatingTextFollowPlayer(int64_t id, const std::string& name);
    // Viewer allow list: only these player names render the text (an
    // empty list = everyone). Armed viewers that fell out are despawned
    // immediately. Unknown ids fail.
    bool setFloatingTextViewers(int64_t id, std::vector<std::string> names);
    // Position + text snapshot of one floating text, for list queries.
    struct FloatingTextSnapshot {
        int64_t id;
        double x, y, z;
        std::string text;  // whole wire nametag (title + '\n' + body)
        std::string title; // top line ("" = body-only tag)
        std::string body;  // lines below the title
        bool visible;
        int64_t followTarget; // entity id anchored over, 0 = static
        size_t viewers;       // players currently rendering it
    };
    // Snapshots every floating text of this world, ordered by entity id.
    std::vector<FloatingTextSnapshot> floatingTexts() const;
    // One-text snapshot; false for unknown ids.
    bool floatingText(int64_t id, FloatingTextSnapshot& out) const;
    // Despawns a floating text and forgets it. Unknown ids fail.
    bool removeFloatingText(int64_t id);
    // Viewer-churn pass: spawns to players that entered render range,
    // despawns from players that left, quit or switched worlds. Driven
    // from tick() once per particle::FloatingTextParticle::kViewerSyncTicks;
    // a world without floating texts pays nothing.
    void tickFloatingTexts(uint64_t current_tick);
    // Follow pass: repositions every follow-mode text whose target moved
    // past the threshold, and auto-removes texts whose target despawned.
    // Driven from tick() every tick but touches only the follow-mode
    // subset; a world without follow texts pays nothing.
    void tickFollowTexts(uint64_t current_tick);

    // ----- Mob entities -----
    // Protocol-84 mobs spawned by the creative spawn eggs or the plugin
    // APIs. The record stays a lightweight value: the AI layer in
    // entity/ai/ owns the per-mob behavior state and drives records
    // through moveMob on the main thread (budgeted pass per tick, gated
    // on player presence — docs/MOB_AI.md). Clients get one
    // AddEntityPacket on spawn, MoveEntityPacket per applied move, one
    // RemoveEntityPacket on removal. Server records are lightweight, so
    // mobs survive chunk unload; a viewer that streams their chunk later
    // re-receives them (sendMobsInChunk, deduplicated per viewer).
    //
    // @p type is a protocol-84 network id from entity::EntityTypes; ids
    // outside the registry fail (-1) because the 0.15.10 client has no
    // verified renderer for them. y is the FEET position; the mob is
    // registered in the chunk that contains it for the resend index.
    // Returns the entity id, or -1 on invalid type/position or when the
    // per-world cap is reached. Ids do not survive restarts.
    // @p head_yaw is the head's own facing; kHeadYawFollowBody (the
    // default) makes the head ride the body yaw — the pre-head-sync
    // contract the plugin bindings were written against.
    int64_t spawnMob(int32_t type, double x, double y, double z, float yaw = 0.0f,
                     float pitch = 0.0f, float head_yaw = kHeadYawFollowBody);
    // Repositions a mob for its current viewers (index follows the move).
    // Unknown ids or out-of-height positions fail. @p head_yaw follows
    // the body under the same sentinel contract as spawnMob.
    bool moveMob(int64_t id, double x, double y, double z, float yaw = 0.0f,
                 float pitch = 0.0f, float head_yaw = kHeadYawFollowBody);
    // Despawns a mob and forgets it. Unknown ids fail.
    bool removeMob(int64_t id);

    // ----- Mob combat -----
    // The vanilla-shaped damage pipeline for world mobs (records, not
    // entity::Living objects). Fires the cancellable EntityDamage
    // (ByEntity)Event with the mob victim identity (entity == nullptr,
    // victimEid/victimType set), applies the (possibly modified) amount,
    // broadcasts the hurt flash to the chunk viewers, and on lethal hits
    // runs the death sequence: EntityDeathEvent, the vanilla drop table,
    // the death animation, and a ~1s window before tickMobDeaths removes
    // the record. A damager adds era knockback (client motion packet plus
    // an AI carry-over the next actuation pass integrates).
    //
    // Returns Hurt/Killed/Cancelled following entity::DamageApply.
    entity::DamageApply damageMob(int64_t eid, float amount,
                                  entity::Entity* damager = nullptr,
                                  event::entity::DamageCause cause =
                                      event::entity::DamageCause::ENTITY_ATTACK);

    // Direct health write, clamped into [0, maxHealth]. No damage event
    // fires (this is not vanilla damage); reaching zero runs the same
    // death sequence as a lethal hit. False for unknown or dying mobs.
    bool setMobHealth(int64_t eid, float hp);

    // Raises a mob's health ceiling (its current health is clamped down
    // when it exceeds the new one). False for unknown or dying mobs.
    bool setMobMaxHealth(int64_t eid, float max);

    // Removes mobs whose death-animation window elapsed. Driven from
    // tick(); an empty death window costs one counter read.
    void tickMobDeaths(uint64_t current_tick);

    // INTERNAL (entity/ai actuation only): the mutex guarding the mob
    // records. The AI consumes the cross-thread knockback fields under
    // it; every other surface goes through the methods above.
    std::mutex& mobMutex() {
        return mobs_mtx_;
    }

    /// @brief Broadcasts one EntityEventPacket to the mob's chunk
    /// viewers (the sheep eat-grass animation is the AI caller).
    void mobEntityEvent(int64_t eid, int32_t cx, int32_t cz, uint8_t event);

    // Snapshot of ONE chunk's mob records (small vector under the mob
    // lock; pointers stable until removeMob). The AI pass visits the
    // world chunk by chunk through this instead of materializing a
    // whole-population list per tick.
    std::vector<MobEntity*> chunkMobs(int32_t cx, int32_t cz);

    // Public chunk key packing (the same layout the internal index
    // uses): the AI tier machinery builds its visited-chunk key list
    // from player coordinates through this.
    static uint64_t chunkKey(int32_t cx, int32_t cz) {
        uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(cx));
        uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(cz));
        return (ux << 32) | uz;
    }

    // Sends one or more BYTE entity-data entries for one mob to its chunk
    // viewers in a single SetEntityDataPacket — the channel the 0.15.10
    // client reads mob poses from (the skeleton bow-draw charging flag is
    // key 5, the creeper swell keys are 16/17/18; see docs/MOB_AI.md for
    // the binary mapping). No-op for unknown mobs.
    bool setMobDataBytes(int64_t id, int32_t cx, int32_t cz,
                         std::initializer_list<std::pair<uint8_t, int8_t>>
                             entries);

    // Returns a non-owning pointer to a mob, or nullptr.
    MobEntity* mob(int64_t eid);
    // Snapshot of all mobs for plugin queries.
    std::vector<MobEntity*> mobs();
    size_t mobCount();

    // Sends AddEntityPacket for every mob in the chunk that @p viewer has
    // not received yet. Called when a chunk streams to a player, so
    // viewers that arrive later (join, walk-in) still see the mobs. The
    // per-viewer dedup lives on the Player (known_mobs_); zero cost for
    // viewers who already hold the entity.
    void sendMobsInChunk(int32_t cx, int32_t cz, Player* viewer);

    // Sends the AddItemEntityPacket for every dropped item currently in one
    // chunk to one player. Rides the chunk stream (and the post-spawn pass)
    // so items that existed before the player entered range — drops from
    // other players, mob loot, previous sessions — render for them too; the
    // era reference spawns exactly this way (sendChunk -> spawnTo() for the
    // chunk's entities). There is no per-viewer dedup on purpose: the 0.15.10
    // client's MultiPlayerLevel::putEntity removes the stale entity and
    // inserts the new one when an eid repeats, so a re-send is a safe
    // position/motion resync rather than a duplicate.
    void sendDroppedItemsInChunk(int32_t cx, int32_t cz, Player* viewer);

    // Removes every entity of this world from one player's view (mobs,
    // dropped items, arrows, falling blocks, floating-text carriers) and
    // forgets the mob ids. Sent when the player switches to another world
    // in the SAME dimension: that client does not clear its entity map on
    // the switch, so anything left behind would render as a ghost in the
    // destination world. Idempotent per entity (unknown ids are ignored).
    void despawnEntitiesFor(Player* viewer);

    // ----- Falling-block physics -----
    // Converts a supported falling block into an entity and checks the block above.
    void spawnFallingBlock(int32_t bx, int32_t by, int32_t bz);

    // Updates falling-block physics, landing, damage, sounds, and cleanup.
    void tickFallingBlocks(uint64_t current_tick);

    // Converts unsupported sand, gravel, or anvils into falling entities.
    void checkFallingSupport(int32_t x, int32_t y, int32_t z);

    // Checks whether a block follows gravity in MCPE 0.15.10.
    static bool isFallableBlockId(uint8_t id);

    // Drops an item at a world position without player context.
    void dropItemAt(double x, double y, double z, uint16_t itemId, uint8_t count, uint16_t damage);

    // Updates block entities such as furnaces once per server tick.
    size_t tickTiles(uint64_t current_tick);

    // Removes expired or claimed dropped items during global garbage collection.
    size_t collectDroppedItems(uint64_t current_tick);

    // Returns the configured world generator.
    std::shared_ptr<generator::Generator> generator() const {
        return generator_;
    }

    // Returns the active world provider.
    std::shared_ptr<format::WorldProvider> provider() const {
        return provider_;
    }

    // Registers or unregisters chunk loaders.
    void registerLoader(uint64_t loader_id, class ChunkLoader* loader);
    void unregisterLoader(uint64_t loader_id);

    // ----- Dropped-item registry -----
    // Returns a non-owning pointer to a dropped item, or nullptr.
    DroppedItem* droppedItem(int64_t eid);

    // Returns a snapshot of dropped items for pickup scans.
    std::vector<DroppedItem*> droppedItems();

    // Registers a newly dropped item.
    void addDroppedItem(const DroppedItem& di);

    // Marks an item for removal.
    void removeDroppedItem(int64_t eid);

    // Atomically claims an item to prevent duplicate pickups.
    bool tryClaimDroppedItem(int64_t eid);

    // ----- Tile-entity registry -----
    // Maintains persistent tile instances for chests, furnaces, signs, and other blocks.
    std::shared_ptr<cursestone::tile::Tile> getTile(int32_t x, int32_t y, int32_t z);
    void setTile(int32_t x, int32_t y, int32_t z, std::shared_ptr<cursestone::tile::Tile> t);
    void removeTile(int32_t x, int32_t y, int32_t z);

    // Drops the resident chunk's stored tile blob for @p pos, if any.
    // MAIN THREAD ONLY (it walks the live chunk's blob list that the tick
    // thread owns and the generation workers never touch). The companion
    // of removeTile() on the player-edit paths: the blob is what the next
    // hydration pass would re-register — and then heal the block back for
    // — resurrecting a chest the player just broke (the phantom chest that
    // survives breaks and reappears after a relog). The generation-spill
    // path does NOT call this: its blobs are reconciled at install time.
    void dropResidentTileBlob(int32_t x, int32_t y, int32_t z);

    // Materializes the tile a structure-chest NBT blob describes (id/x/y/z
    // + payload) into the registry — only if the position is free. Wired
    // as ChunkManager's tile-blob sink: when a chest blob generated by a
    // neighbor population lands in a live chunk, the tile exists beside
    // its block immediately instead of waiting for the next hydration
    // pass (which a tile_entities rebuild from the registry between the
    // two moments would otherwise wipe). Never overwrites: a live tile —
    // hydration-made or player-made — is always newer than the blob.
    void applyTileBlob(const uint8_t* data, size_t len);

    // Returns all tile entities in a chunk for client synchronization.
    std::vector<std::shared_ptr<cursestone::tile::Tile>> getTilesInChunk(int32_t cx, int32_t cz);

    // Drops the "tiles hydrated" marker of a chunk so its blocks are
    // rescanned on the next tile access. The chunk stream must carry the
    // CURRENT tile set every time it goes out (chunk data, then tile NBTs):
    // a client can drop and re-request a chunk at any moment (render-radius
    // changes, leaving and coming back), and blocks that became tile blocks
    // after the first hydration — structure population writing into a
    // resident chunk, plugin setBlock, a furnace placed before a round
    // trip — would otherwise reach that client WITHOUT their block actor.
    // The 0.15.10 client only binds a container UI to an actor it already
    // holds; a furnace screen opened against a missing actor takes the
    // game down.
    void invalidateTileHydration(int32_t cx, int32_t cz);

    // ----- Chunk memory eviction -----
    // Stores a viewer's current chunk, configured view radius and world. A
    // viewer only protects chunks of its OWN world: coordinates overlap
    // between worlds (overworld spawn vs the 1:8-scaled nether) used to
    // pin chunks that nobody ever visited.
    struct ChunkViewer {
        int32_t cx, cz, view_radius;
        const World* world = nullptr;
    };

    // Unloads chunks outside every own-world viewer radius, saving dirty
    // chunks first. At most @p max_chunks chunks per call (0 = unlimited);
    // MemoryManager passes a budget so an idle world drains over several
    // GC passes instead of stalling one tick.
    int unloadDistantChunks(const std::vector<ChunkViewer>& viewers, int32_t margin = 4,
                            size_t max_chunks = 0);

    // Chunks currently resident in memory (the chunk cache size).
    size_t chunkCount() const {
        return chunks_.count();
    }

  private:
    double lastTickMs_ = 0.0;
    double avgTickMs_ = 0.0;
    double avgLightMs_ = 0.0;

    // Serializes one LevelEventPacket and delivers it to every player that
    // sees the chunk (cx, cz). Every particle world-event broadcast funnels
    // through here: the packet is built once per event (never re-encoded per
    // player) and the viewer filter (spawned, in this world, not pending
    // removal, chunk in view) stays identical across all callers.
    void broadcastLevelEvent(int16_t evid, float x, float y, float z,
                             int32_t data, int32_t cx, int32_t cz);

    // Flushes a chunk's tiles and removes their in-memory registry entries.
    void evictTilesForChunk(int32_t cx, int32_t cz);

    // Loads tile entities from a chunk's stored NBT data. When
    // @p notifyViewers is true (every caller except the chunk-stream tile
    // pass, which sends its tiles itself), tiles born during this pass are
    // announced to the viewers that already hold the chunk — a tile created
    // outside a stream would otherwise stay silent until the next re-stream,
    // which for a normal 0.15 session never comes.
    void ensureTilesLoaded(int32_t cx, int32_t cz, bool notifyViewers = true);

    // Creates missing tile entities for blocks that require them. Tiles it
    // registers are appended to @p freshOut when that is non-null.
    void hydrateTilesFromBlocks(
        int32_t cx, int32_t cz, mcp_chunk_t* chunk,
        std::vector<std::shared_ptr<cursestone::tile::Tile>>* freshOut = nullptr);

    // Announces one tile's spawn NBT (BlockEntityData 0x32) to every spawned
    // viewer that tracks the tile's chunk — the world-side twin of
    // Player::sendChunkTiles, used wherever a tile is born outside a chunk
    // stream (hydration on a getTile/save pass, legacy heals).
    void spawnTileToViewers(cursestone::tile::Tile& t);

    // Writes dirty tile entities back into their owning chunks before saving.
    void syncTilesToChunks();

    WorldConfig cfg_;
    ChunkManager chunks_;
    std::shared_ptr<format::WorldProvider> provider_;
    std::shared_ptr<generator::Generator> generator_;
    PortalRegistry portals_;   // Persistent Nether portal registry.
    std::string portals_path_; // Path to the portal registry file.
    std::string legacy_mobs_path_; // Pre-region mob file, read once for migration.
    mcp_vector3_t spawn_;
    std::unordered_map<uint64_t, class ChunkLoader*> loaders_;

    // ----- Weather state -----
    int weather_state_ = WEATHER_CLEAR;
    int32_t weather_ticks_left_ = -1;       // -1 means unlimited duration.
    uint32_t rain_strength_ = 100000;       // Rain event strength.
    uint32_t thunder_strength_ = 35000;     // Thunder event strength.
    uint64_t rain_scan_offset_ = 0;         // Round-robin chunk scan cursor.
    uint64_t last_fire_extinguish_log_ = 0; // Fire log rate limit.
    uint64_t next_lightning_tick_ = 0;      // Next permitted lightning tick.

    // Skip-night countdown; 0 means inactive.
    int32_t sleep_ticks_ = 0;

    // Countdown to the next periodic time re-anchor broadcast.
    uint32_t time_resync_countdown_ = TIME_RESYNC_INTERVAL;

    // Selects a random online player for a lightning strike during thunderstorms.
    void strikeRandomPlayerLightning();

    // Dropped items indexed by runtime entity ID.
    std::unordered_map<int64_t, std::unique_ptr<DroppedItem>> dropped_items_;
    mutable std::mutex dropped_items_mtx_;
    std::unordered_map<int64_t, std::unique_ptr<ArrowEntity>> arrows_;
    mutable std::mutex arrows_mtx_;

    // Thrown splash potions indexed by runtime entity ID.
    std::unordered_map<int64_t, std::unique_ptr<SplashPotionEntity>> splash_potions_;
    mutable std::mutex splash_potions_mtx_;

    // Thrown experience bottles indexed by runtime entity ID.
    std::unordered_map<int64_t, std::unique_ptr<ExperienceBottleEntity>>
        xp_bottles_;
    mutable std::mutex xp_bottles_mtx_;

    // Experience orbs indexed by runtime entity ID. The pickup takes the
    // orb record while only holding xp_orbs_mtx_, never the player lock.
    std::unordered_map<int64_t, std::unique_ptr<ExperienceOrbEntity>> xp_orbs_;
    mutable std::mutex xp_orbs_mtx_;

    // Active effects per mob eid (see addMobEffect). Guarded by its own
    // mutex; lock order is always mobs_mtx_ then mob_effects_mtx_.
    std::unordered_map<int64_t, std::vector<std::shared_ptr<entity::Effect>>>
        mob_effects_;
    mutable std::mutex mob_effects_mtx_;

    // --- Mob effect helpers (splash potions, /effect) ---
    // Vanilla ThrownPotion::onHit candidate box: hit point grown by (4, 2, 4).
    static bool withinSplashVolume(double px, double py, double pz, double ex,
                                   double ey, double ez);
    // Vanilla ThrownPotion::_dowseFire: water splash clears fire blocks at the
    // hit cell, its four horizontal neighbours and the cell above.
    void dowseFireAround(float x, float y, float z);
    // MobEffectPacket (0x19) with vanilla REMOVE zeroing, to the mob's
    // chunk viewers.
    void sendMobEffectToViewers(int64_t eid, int32_t cx, int32_t cz,
                                const entity::Effect& e, uint8_t event_id);
    // DATA_POTION_COLOR / DATA_POTION_AMBIENT for a mob to its viewers.
    void broadcastMobPotionColor(
        int64_t eid, int32_t cx, int32_t cz,
        const std::vector<std::shared_ptr<entity::Effect>>& vec);
    float mobHealth(int64_t eid);
    float mobMaxHealthOf(int64_t eid);
    void healMob(int64_t eid, float amount);

    // Falling blocks indexed by runtime entity ID.
    std::unordered_map<int64_t, std::unique_ptr<FallingBlockEntity>> falling_blocks_;
    mutable std::mutex falling_blocks_mtx_;

    // Floating texts indexed by runtime entity ID (the carrier's id).
    // particle::FloatingTextParticle owns the wire state and the viewer
    // set; the map only ties ids to instances. follow_texts_ indexes the
    // follow-mode subset so the per-tick follow pass touches only those.
    static constexpr size_t kMaxFloatingTexts = 128;
    std::unordered_map<int64_t, std::unique_ptr<particle::FloatingTextParticle>>
        floating_texts_;
    std::unordered_set<int64_t> follow_texts_;
    mutable std::mutex floating_texts_mtx_;

    // Standing mobs indexed by runtime entity ID. Static records (the
    // AI layer in entity/ai/ owns all per-mob dynamic state and drives
    // them through moveMob) — the per-viewer AddEntityPacket resend
    // rides on chunk streaming through the chunk index below. The
    // record ceiling is the per-world config (WorldConfig::max_mobs,
    // the max-mobs property), not a compile-time constant.
    std::unordered_map<int64_t, std::unique_ptr<MobEntity>> mobs_;
    mutable std::mutex mobs_mtx_;

    // Mobs inside the death-animation window (death_tick != 0). Kept
    // under mobs_mtx_ so tickMobDeaths early-outs on the common empty
    // case without touching the map.
    size_t dying_mobs_ = 0;

    // Per-chunk mob index: chunk key -> eids. Mirrors mobs_ under the
    // same mutex (mobs_ stays the authority) so a chunk stream touches
    // O(mobs of the chunk) instead of scanning the whole world map —
    // the same pattern the tile registry uses with tiles_by_chunk_.
    std::unordered_map<uint64_t, std::vector<int64_t>> mobs_by_chunk_;

    // Chunk keys whose mob roster changed since the last save pass
    // (spawn, move, damage, death). Drained by syncMobsToChunks at every
    // save point; only chunks that actually changed pay for an entity
    // list rebuild. Lives under mobs_mtx_ beside the state it mirrors.
    std::unordered_set<uint64_t> mobs_dirty_chunks_;

    // Marks a chunk's mob roster changed. Caller holds mobs_mtx_ (every
    // mutation point already runs under it, so this stays an insert).
    void markMobsDirtyLocked(int32_t cx, int32_t cz) {
        mobs_dirty_chunks_.insert(packChunkPos(cx, cz));
    }

    // Rebuilds one resident chunk's entity list from its live records
    // (MobChunkCodec::writeChunkEntities) and dirties the chunk only when
    // the bytes actually changed. Snapshot runs under mobs_mtx_, the
    // chunk write happens outside it.
    void rebuildMobBlobs(int32_t cx, int32_t cz, mcp_chunk_t* chunk);

    // Mob restore at the chunk boundary: called once per chunk that
    // enters the level (any install path). Parsable rows in the chunk's
    // entity list become MobEntity records with fresh eids (no packets —
    // viewers pick them up through sendMobsInChunk when they stream the
    // chunk); a roster already in memory for this chunk (a mob a plugin
    // moved into not-yet-resident terrain) is newer than the disk list
    // and wins instead. Runs on install threads, so it only locks
    // mobs_mtx_ and never touches the wire.
    void reconcileMobsForChunk(int32_t cx, int32_t cz);

    // One-time import: a world saved by a pre-region build still carries
    // mobs.dat beside level.dat. Its rows are written into the owning
    // chunks' entity lists (chunks forced resident, flushed to disk),
    // then the legacy file is removed — from then on the region files
    // are the only mob storage.
    void migrateLegacyMobs();

    // Mob AI driver (entity/ai/): owned here so its lifetime is the
    // world's, drained from tick() after the falling blocks. The type
    // is forward-declared; the destructor lives in World.cpp where the
    // full header is visible.
    std::unique_ptr<entity::ai::MobAIManager> mob_ai_;

    // Tile entities indexed by packed block position.
    std::unordered_map<uint64_t, std::shared_ptr<cursestone::tile::Tile>> tiles_;
    mutable std::mutex tiles_mtx_;

    // Per-chunk tile index: chunk key -> slots of (packed tile pos, tile).
    // Mirrors tiles_ under the same mutex (tiles_ stays the authority for
    // position lookups) so chunk-send, save grouping and eviction touch
    // O(tiles of the chunk) instead of scanning the whole world map
    // (CURSESTONE_ARCHITECTURE_AUDIT #16). The packed position lives in
    // each slot so removal and eviction never re-derive keys from the
    // tile's float position. Chunks without tiles carry no bucket.
    struct TileSlot {
        uint64_t key;
        std::shared_ptr<cursestone::tile::Tile> tile;
    };
    std::unordered_map<uint64_t, std::vector<TileSlot>> tiles_by_chunk_;

    // Tracks chunks whose tile NBT has been loaded this session.
    std::unordered_set<uint64_t> tiles_hydrated_chunks_;

    // Tile-hydration gate (PERF): per chunk, the block-edit serial the
    // last hydration scan saw. ensureTilesLoaded() re-scans a chunk's
    // 32768 cells only when the chunk's block_edit_serial moved since
    // that scan — every stream of an unchanged chunk pays a 32-bit
    // compare instead of a full block walk. Written under tiles_mtx_;
    // the serial itself lives in the chunk and is bumped O(1) by every
    // live block write.
    std::unordered_map<uint64_t, uint32_t> tiles_hydrated_serial_;

    // Tracks chunks whose stored tile NBT must be rebuilt after removals.
    std::unordered_set<uint64_t> tiles_dirty_chunks_;

    // Manages sky and block light propagation.
    LightEngine light_;

    // Dynamic-liquid wake queue (see queueLiquidWake). Chunks are queued
    // from the generation worker thread and drained on the main thread;
    // next_cell_ is the scan cursor so a chunk whose dynamic cells exceed
    // one tick's submission budget resumes where it stopped.
    struct LiquidWake {
        int32_t cx;
        int32_t cz;
        uint32_t next_cell;
    };
    std::deque<LiquidWake> liquid_wake_;
    std::unordered_set<uint64_t> liquid_wake_keys_;
    mutable std::mutex liquid_wake_mtx_;

    static uint64_t packChunkPos(int32_t cx, int32_t cz) {
        uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(cx));
        uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(cz));
        return (ux << 32) | uz;
    }

    // Packs 28-bit X/Z coordinates and an 8-bit Y coordinate into one key.
    static uint64_t packTilePos(int32_t x, int32_t y, int32_t z) {
        uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(x)) & 0xFFFFFFFULL;
        uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(z)) & 0xFFFFFFFULL;
        uint64_t uy = static_cast<uint64_t>(static_cast<uint8_t>(y));
        return (ux << 36) | (uz << 8) | uy;
    }
};

// Broadcasts a single block update to every viewer of the chunk.
void sendUpdateBlock(World* w, int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t data);
} // namespace
