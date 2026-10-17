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
#pragma once
#include "CurseStone.h" // Defines math::Vector3.
#include "IPlayer.h"
#include "entity/Human.h"
#include "world/Location.h"
#include "permission/PermissibleBase.h"
#include "network/AdvancedSourceInterface.h"
#include "Server.h"         // Defines NbtPtr.
#include "item/ItemStack.h" // ItemStack for getDrops().
#include <memory>
#include <string>
#include "utils/MainLogger.hpp"
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <vector>
namespace cursestone {
class Server;
class Network;
class SourceInterface;
namespace world {
class World;
}
namespace network::protocol {
class DataPacket;
class LoginPacket;
class DropItemPacket;
class BlockEntityDataPacket;
}
namespace inventory {
class PlayerInventory;
}
namespace tile {
class Tile;
class TileChest;
class TileFurnace;
}

// Portal anchor and axis resolved during cross-dimension travel.
struct PortalArrival {
    int32_t x = -1, y = 0, z = 0; // -1 means no portal.
    uint8_t axis = 0;
};

struct PlayerConfig {
    std::string username;
    std::string uuid;
    std::string xuid;      // Xbox Live ID.
    std::string client_id; // RakNet GUID.
    // Skin data received during login.
    std::string skin_id;
    std::string skin_data;
    std::string ip;
    uint16_t port = 0;
    int gamemode = 0;
    int view_distance = 8;
    int64_t runtime_eid = 1; // Server-allocated entity ID.
};

class Player : public IPlayer,
               public entity::Human,
               public permission::PermissibleBase,
               public std::enable_shared_from_this<Player> {
  public:
    Player(Server* srv, PlayerConfig cfg);
    ~Player() override;

    // --- IPlayer ---
    std::string getName() const override {
        return cfg_.username;
    }
    // Nametag appears above the player and in the player list.
    // Display name is used in chat; both default to the username.
    const std::string& getNameTag() const {
        return nametag_.empty() ? cfg_.username : nametag_;
    }
    void setNameTag(const std::string& tag); // Broadcast name changes.
    // Reads display_name_ with username fallback.
    void setDisplayName(const std::string& name) {
        display_name_ = name;
    }
    // Skin forwarded to other players.
    const std::string& skinId() const {
        return cfg_.skin_id;
    }
    const std::string& skinData() const {
        return cfg_.skin_data;
    }
    bool isOnline() const override {
        return connected_.load(std::memory_order_relaxed);
    }
    bool isOp() const override;
    void setOp(bool v) override;
    bool isBanned() const override;
    void setBanned(bool v) override;
    bool isWhitelisted() const override;
    void setWhitelisted(bool v) override;
    // Operators inherit the cursestone.* permission subtree.
    bool hasPermission(const std::string& name) const override {
        if (PermissibleBase::hasPermission(name))
            return true;
        if (!isOp())
            return false;
        static const std::string kRoot = "cursestone.";
        return name.size() > kRoot.size() && name.compare(0, kRoot.size(), kRoot) == 0;
    }
    void recalculatePermissions() override {
    }

    // --- Session lifecycle ---
    bool isConnected() const {
        return connected_.load(std::memory_order_relaxed);
    }
    bool loggedIn() const {
        return logged_in_.load(std::memory_order_relaxed);
    }
    void close(const std::string& reason = "disconnect.quitting");

    // --- Network callbacks ---
    void handleDataPacket(network::protocol::DataPacket& p);
    // Logs packet-handler errors and kicks the player with details.
    void handlePacketError(uint8_t pid, const std::string& what);
    void handleLogin(network::protocol::LoginPacket& p);
    void handleMovePlayer(double x, double y, double z, float yaw, float pitch, bool onGround);
    // Updates hunger and environmental damage each tick.
    void tickHungerAndDamage(uint64_t current_tick);
    void handleChat(const std::string& message);
    void handleBlockBreak(int x, int y, int z);
    void handleBlockPlace(int x, int y, int z, uint8_t face, int16_t heldItemId, uint8_t heldCount,
                          uint16_t heldDamage, float fx = 0.5f, float fy = 0.5f, float fz = 0.5f);
    void handlePlayerAction(int action, int x, int y, int z, int face);

    // --- Bow handling ---
    // Starts drawing a bow when the player uses it in the air.
    void handleUseItemAir(int16_t heldItemId);
    // Releases the bow, consumes arrows, and spawns the projectile.
    void releaseBow();

    // Broadcasts this player's sneaking and sprinting flags.
    void broadcastPoseFlags();

    // Broadcasts the bow-use pose (DATA_FLAG_ACTION) to other players;
    // drawing = true raises the flag, false clears it. Deduplicated via
    // bow_pose_active_ so repeated air-use packets cannot spam viewers.
    void broadcastBowPose(bool drawing);

    // True when the inventory holds at least one arrow (creative players
    // are always considered stocked). Gates the bow-use pose so viewers
    // never see a draw animation a bow without ammo cannot back up.
    bool hasArrowInStock() const;

    // Sends the currently held item to every other client (MobEquipment
    // broadcast) so remote players render the item in this player's hand.
    void broadcastHeldItemToViewers();

    // Adds this player to the lists and entity views of other players.
    void announceSelfToOthers();

    // Respawns nearby players for this client.
    // Handles melee attacks against supported player entities.
    void handleInteract(int action, int64_t targetEid);

    // Respawns the player after death.
    void respawn();

    // Opens a chest, creates its tile if needed, and fires InventoryOpenEvent.
    void openChestAt(int x, int y, int z);

    // Closes the currently open chest or furnace.
    void closeOpenChest();

    // Opens a furnace and synchronizes its inventory and progress.
    void openFurnaceAt(int x, int y, int z);

    // Spawns a dropped item and returns its runtime entity ID.
    int64_t dropItem(double x, double y, double z, uint16_t itemId, uint8_t count, uint16_t damage,
                     double motionX = 0.0, double motionY = 0.2, double motionZ = 0.0,
                     uint32_t pickup_delay = 10, const std::string& nbt = "");

    // Returns the unit vector in the direction the player is looking.
    math::Vector3 getDirectionVector() const;

    // Validates and processes a dropped inventory item.
    void handleDropItem(network::protocol::DropItemPacket& pk);

    // --- Floating inventory ---
    // Tracks items temporarily held by the client cursor.
    void floatingAdd(const item::ItemStack& it);
    // Removes matching items from the floating buffer.
    item::ItemStack floatingTake(const item::ItemStack& want);
    bool floatingContains(const item::ItemStack& want) const;
    bool floatingRemove(const item::ItemStack& want);
    // Validates survival-mode slot changes against the floating buffer.
    bool beginSurvivalSlotWrite(inventory::Inventory* inv, int absSlot,
                                const item::ItemStack& target);
    // Honors a client-side pickup write: claims the matching dropped item
    // inside the pickup volume so the write is accepted instead of wiped
    // (the real client picks up drops before the server pickup delay ends).
    bool tryClaimNearbyDrop(const item::ItemStack& want);
    // Returns cursor items to inventory and drops any overflow.
    void returnFloatingItems();

    // Validates sign edits and fires SignChangeEvent.
    void handleSignEdit(network::protocol::BlockEntityDataPacket& pk);

    // Consumes held food and updates hunger.
    void consumeHeldItem();

    // Sends block entity NBT to one player or all chunk viewers.
    void sendBlockEntityData(int32_t x, int32_t y, int32_t z, tile::Tile& t,
                             Player* only = nullptr);

    // Sends tile data for all block entities in a chunk.
    void sendChunkTiles(int32_t cx, int32_t cz);

    // Sends the creative inventory contents.
    void sendCreativeInventory();

    // Synchronizes the main inventory and armor.
    void sendInventory();

    // Confirms an inventory slot change to the client.
    void sendSlot(int slot);

    // Saves player NBT and inventory data.
    void saveData();

    // Checks whether the player tracks a given chunk.
    bool isTrackingChunk(int32_t cx, int32_t cz) const;

    // Checks whether a block's chunk is within the player's view distance.
    bool canSeeBlock(int32_t cx, int32_t cz) const;

    // --- Server-to-player packets ---
    // Sends a packet, optionally using the ordered channel.
    void sendPacket(std::shared_ptr<network::protocol::DataPacket> p, bool ordered = false);
    // Flushes pending packets into batches.
    void flushPendingPackets();
    void sendMessage(const std::string& msg) override;
    void kick(const std::string& reason = "disconnect.kick");
    // Includes diagnostic details in the client's disconnect message.
    void kick(const std::string& reason, const std::string& detail);
    void teleport(double x, double y, double z);

    // --- Spawning ---
    void doFirstSpawn();
    void sendChunksAroundSpawn();
    bool spawned() const {
        return spawned_.load(std::memory_order_relaxed);
    }
    int gamemode() const {
        return cfg_.gamemode;
    }
    void setGamemode(int gm);

    // --- Tick ---
    void onUpdate(uint64_t current_tick);
    // Re-streams chunks after the client's first spawn acknowledgment.
    void handleClientSpawnAck() {
        if (!srv_ || !srv_->getDefaultWorld())
            return;
        // Process only the first spawn acknowledgment.
        bool expected = false;
        if (!spawn_ack_done_.compare_exchange_strong(expected, true))
            return;
        {
            std::lock_guard<std::mutex> lk(chunks_mtx_);
            used_chunks_.clear();
        }
        // Restart chunk streaming around the player's current position.
        int cx = (int)(pos_.x) >> 4;
        int cz = (int)(pos_.z) >> 4;
        {
            std::lock_guard<std::mutex> lk(queue_mtx_);
            chunk_queue_.clear();
            chunk_queue_.emplace_back(cx, cz);
        }
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[CHUNKS] Client spawn ACK for " + cfg_.username +
                                          " - re-streaming view radius " +
                                          std::to_string(cfg_.view_distance));
        }
    }

    // --- Inventory ---
    std::shared_ptr<inventory::PlayerInventory> getInventory() const {
        return inv_;
    }

    // Returns the inventory of the currently open container.
    inventory::Inventory* openContainerInventory() const;

    // Checks whether a chest or furnace is currently open.
    bool hasOpenContainer() const {
        return open_chest_ != nullptr || open_furnace_ != nullptr;
    }

    // --- Chunk coordinates ---
    int chunkX() const {
        return (int)(pos_.x) >> 4;
    }
    int chunkZ() const {
        return (int)(pos_.z) >> 4;
    }
    int viewDistance() const {
        return cfg_.view_distance;
    }

    // --- Network session ---
    // The network interface is owned by Network and is not transferred.
    void setSourceInterface(network::AdvancedSourceInterface* iface) {
        adv_iface_ = iface;
    }
    network::AdvancedSourceInterface* sourceInterface() const {
        return adv_iface_;
    }
    // Timestamp of the last received packet.
    uint64_t lastActivityMs() const {
        return last_activity_ms_;
    }
    Server* server() const {
        return srv_;
    }
    const PlayerConfig& config() const {
        return cfg_;
    }
    const std::string& ip() const {
        return cfg_.ip;
    }
    uint16_t port() const {
        return cfg_.port;
    }

    // --- Player NBT ---
    NbtPtr namedTag() const {
        return named_tag_;
    }
    void setNamedTag(NbtPtr n) {
        named_tag_ = std::move(n);
    }

    // --- Gamemode helpers ---
    bool isSurvival() const {
        return cfg_.gamemode == 0;
    }
    bool isCreative() const {
        return (cfg_.gamemode & 1) != 0;
    }
    // Checks whether the player's burn timer is active.
    bool isOnFire() const {
        return fire_ticks_ > 0;
    }
    bool isAdventure() const {
        return cfg_.gamemode == 2;
    }
    bool isSpectator() const {
        return cfg_.gamemode == 3;
    }

    // --- Player information ---
    // Returns the configured display name or the username.
    const std::string& getDisplayName() const {
        return display_name_.empty() ? cfg_.username : display_name_;
    }
    std::string getAddress() const {
        return cfg_.ip;
    }
    int64_t getClientId() const {
        return std::stoll(cfg_.client_id.empty() ? "0" : cfg_.client_id);
    }
    bool hasPlayedBefore() const {
        return named_tag_ != nullptr;
    }

    // --- Position and teleport ---
    math::Vector3 getNextPosition() const {
        return pos_;
    }
    // Returns the personal bed spawn or the world spawn.
    math::Vector3 getSpawn() const;
    void setSpawn(int x, int y, int z, bool save);
    // Returns the server spawn adjusted to safe terrain.
    math::Vector3 getServerSpawnSafe() const;
    void sendPosition(double x, double y, double z, double yaw = 0, double pitch = 0,
                      uint8_t mode = 0);

    // --- Chat and messaging ---
    void sendPopup(const std::string& msg);
    void sendTip(const std::string& msg);
    void sendTranslation(const std::string& key);

    // --- Experience ---
    void setExperience(int exp, int level);
    void sendExperience(int exp);
    void sendExperienceLevel(int level);

    // --- Health and food ---
    void setHealth(int h);
    // Heals and syncs the hearts attribute right away (potion effects
    // heal outside the hunger tick loop, which otherwise never pushes).
    void heal(float amount) override;
    // Synchronizes hunger, saturation, and exhaustion attributes.
    void sendFoodLevel();
    // Adds exhaustion; hunger drains after the exhaustion threshold.
    void addExhaustion(float amount);
    float foodExhaustion() const {
        return food_exhaustion_;
    }
    float foodSaturation() const {
        return food_saturation_;
    }
    // Sends updated health attributes.
    void sendHealthAttribute();
    // Sends health and hunger attributes.
    void sendAllAttributes();
    bool isFoodEnabled() const {
        return cfg_.gamemode == 0;
    }

    // --- Chunk management ---
    bool isChunkLoaded(int cx, int cz) const;
    bool isChunkLoading(int cx, int cz) const;
    void unloadChunk(int cx, int cz);
    void sendChunk(int cx, int cz);

    // --- Visibility ---
    bool canSee(Player* other) const {
        return other != nullptr;
    }
    void hidePlayer(Player* /*other*/) {
    }
    void showPlayer(Player* /*other*/) {
    }
    void spawnTo(Player* player);
    void announceTo(Player* viewer);

    // --- Sneaking ---
    // Reports whether the client is sneaking.
    bool isSneaking() const {
        return sneaking_;
    }
    void setSneaking(bool v);

    // --- Sleeping ---
    // Starts sleeping if the bed is available and the event allows it.
    bool sleepOn(int bx, int by, int bz);
    // Checks whether the player is currently sleeping.
    bool isSleeping() const {
        return sleeping_;
    }
    // Stops sleeping and resets the world sleep state.
    void stopSleep();
    // Returns the coordinates of the bed head.
    void sleepPos(int& bx, int& by, int& bz) const {
        bx = sleep_x_;
        by = sleep_y_;
        bz = sleep_z_;
    }
    // True while the personal spawn point overrides the world spawn (bed spawn).
    bool hasSpawnOverride() const {
        return has_spawn_override_;
    }
    // Returns the personal spawn coordinates (bed head when set).
    void spawnPos(int& sx, int& sy, int& sz) const {
        sx = spawn_x_;
        sy = spawn_y_;
        sz = spawn_z_;
    }
    // Wakes players sleeping in or near the specified bed.
    static void wakeSleepersOnBed(Server* srv, world::World* lvl, int bx, int by, int bz);
    // Clears the personal spawn of every player whose respawn point is the
    // specified bed (any of its halves) and points them back at the world spawn.
    static void clearSpawnsOnBed(Server* srv, world::World* lvl, int bx, int by, int bz);
    // Drops the personal spawn override back to the world spawn (bed broken
    // or missing, or bed spawn disabled) and informs the client.
    void resetSpawnToDefault();

    // --- Interaction ---
    bool canInteract(math::Vector3 pos, double maxDistance) const;
    bool canCollideWith(Entity* /*e*/) const {
        return false;
    }
    void resetFallDistance() {
    }

    // --- Flight ---
    bool getAllowFlight() const {
        return (cfg_.gamemode & 1) != 0;
    }
    void setAllowFlight(bool /*v*/) {
    }
    bool hasAutoJump() const {
        return true;
    }
    void setAutoJump(bool /*v*/) {
    }

    // --- Drops ---
    std::vector<cursestone::item::ItemStack> getDrops() const;

    // --- Windows ---
    int getWindowId(void* /*inventory*/) const {
        return -1;
    }
    int addWindow(void* /*inventory*/, int /*forceId*/ = -1) {
        return -1;
    }
    void removeWindow(void* /*inventory*/) {
    }

    // --- Crafting ---
    // Processes the crafting recipe and its reported inputs and outputs.
    void handleCraftingEvent(const std::array<uint8_t, 16>& recipe_uuid,
                             const std::vector<cursestone::item::ItemStack>& input,
                             const std::vector<cursestone::item::ItemStack>& output,
                             uint8_t window_id);

    // Matches and executes recipes in the 2x2 inventory grid.
    void handleInventoryCraft();

    // --- Dimensions ---
    // Returns the player's current world.
    World* getWorld() const;
    // Switches worlds and fires a cancellable teleport event.
    bool switchWorld(World* target, double x, double y, double z, bool throughPortal);
    // Travels to the Nether, scaling coordinates and resolving a portal.
    bool teleportToNether();
    // Returns to the Overworld, scaling coordinates and resolving a portal.
    bool teleportToOverworld();
    // Checks portal contact and initiates dimensional travel.
    void checkPortal();
    // Completes a dimension switch after chunks have streamed.
    void finishDimensionSwitch();

    // --- Settings ---
    void setMovementSpeed(float speed);
    void setAttribute(int attrId, float value);
    void sendSettings();

    // --- Potion effects (protocol 84 MobEffectPacket) ---
    // Sends the movement-speed attribute derived from the walk speed
    // base and the active speed/slowness effects.
    void sendMovementSpeedAttribute();
    // Re-sends every active effect as an ADD packet (used at first spawn
    // after a login that restored persisted effects).
    void sendActiveEffects();
    // Throws the held splash potion as entity type 86 and consumes it.
    void throwSplashPotion();

    // --- Killer ---
    Entity* getKiller() const {
        return nullptr;
    }

    // Base damage dealt by an unarmed attack.
    static constexpr float BASE_ATTACK_DAMAGE = 1.0f;
    // Minimum server-side interval between melee attacks.
    static constexpr uint64_t ATTACK_COOLDOWN_MS = 250;

    // Returns the current container window ID.
    uint8_t containerWindowId() const {
        return container_window_id_;
    }

    // Allocates the next container window ID.
    uint8_t openContainerWindow();

    // --- Batch packets ---
    bool batchDataPacket(const std::vector<uint8_t>& data);
    bool dataPacket(std::shared_ptr<network::protocol::DataPacket> pk) {
        sendPacket(pk);
        return true;
    }
    bool directDataPacket(std::shared_ptr<network::protocol::DataPacket> pk) {
        sendPacket(pk);
        return true;
    }

  protected:
    // Handles player death events and broadcasts the death message.
    void onDeath() override;
    // Broadcasts the hurt animation and updates health attributes.
    void onHealthChanged() override;

    // Effect lifecycle hooks (entity::Living): sync the effect to the
    // owner's client and to viewers, and refresh derived client state.
    void onEffectAdded(const entity::Effect& e) override;
    void onEffectModified(const entity::Effect& e) override;
    void onEffectRemoved(const entity::Effect& e) override;
    // Hunger effect interval: drains exhaustion like vanilla.
    void onHungerEffectTick(int amplifier) override;
    // Saturation effect interval: restores food and saturation.
    void onSaturationEffectTick(int amplifier) override;

  private:
    // Sends one MobEffectPacket (self eid=0 + same-world viewers).
    void sendEffectPacket(const entity::Effect& e, uint8_t eventId);
    // Recomputes the blended effect tint and broadcasts it as
    // DATA_POTION_COLOR / DATA_POTION_AMBIENT metadata to same-world
    // viewers (the reference Entity::recalculateEffectColor).
    void broadcastPotionColor();

  public:
    // --- Damage pipeline ---
    // Creative players ignore ordinary damage, except void and custom causes.
    entity::DamageApply damage(float amount) override;
    entity::DamageApply damage(float amount, event::entity::DamageCause cause) override;
    entity::DamageApply damageByEntity(
        float amount, entity::Entity* damager,
        event::entity::DamageCause cause = event::entity::DamageCause::ENTITY_ATTACK) override;
    // Updates the burn timer and broadcasts the fire metadata flag.
    void setOnFire(int seconds) override;

  private:
    Server* srv_;
    PlayerConfig cfg_;
    network::AdvancedSourceInterface* adv_iface_ = nullptr;
    std::shared_ptr<inventory::PlayerInventory> inv_;
    // Borrowed pointer; Server owns world instances.
    World* world_ = nullptr;
    // Only one chest or furnace can be open at a time.
    std::shared_ptr<tile::TileChest> open_chest_;
    std::shared_ptr<tile::TileFurnace> open_furnace_;
    // Active and next container window IDs.
    uint8_t container_window_id_ = 0;
    uint8_t next_container_window_id_ = 2;
    // Items temporarily held by the client cursor.
    std::vector<item::ItemStack> floating_items_;
    NbtPtr named_tag_; // Player data.
    std::atomic<bool> connected_{false};
    std::atomic<bool> logged_in_{false};
    std::atomic<bool> spawned_{false};

    // Delays first spawn until enough chunks reach the client.
    std::atomic<bool> awaiting_first_spawn_{false};
    static constexpr int FIRST_SPAWN_CHUNK_THRESHOLD = 9; // 3x3 area.
    uint64_t last_save_tick_ = 0;
    uint64_t start_time_ms_ = 0;    // Session start time.
    uint64_t last_activity_ms_ = 0; // Last packet timestamp.
    uint64_t last_attack_ms_ = 0;   // Last accepted melee attack.
    // --- Nametag and display name ---
    std::string nametag_;      // Empty means username.
    std::string display_name_; // Empty means username.
    // --- Bow draw state ---
    int64_t bow_start_action_ = -1; // Start tick; -1 means inactive.
    bool bow_pose_active_ = false;  // Action pose currently broadcast to viewers.

    // --- Hunger and environmental damage ---
    float food_saturation_ = 5.0f;   // Saturation drains before food.
    float food_exhaustion_ = 0.0f;   // Exhaustion accumulator.
    uint64_t last_eat_ms_ = 0;       // Eating/drinking cooldown.
    float movement_speed_base_ = 0.1f; // Walk speed before effect multipliers.
    bool invisible_ = false;         // Invisibility effect pose bit.
    float tick_move_dist_ = 0.0f;    // Horizontal distance moved this tick.
    bool food_attrs_dirty_ = false;  // Food attributes need syncing.
    uint64_t last_pos_y_ = 0;        // Previous Y position.
    bool last_pos_set_ = false;      // Fall-damage initialization guard.
    bool was_in_lava_ = false;       // Lava damage state.
    bool was_on_fire_ = false;       // Previous fire state.
    uint64_t last_fire_dmg_ms_ = 0;  // Fire damage interval.
    uint64_t last_lava_dmg_ms_ = 0;  // Lava damage interval.
    uint64_t last_fire_tick_dmg_ms_ = 0;
    uint64_t last_drown_dmg_ms_ = 0; // Drowning damage interval.
    uint64_t last_cactus_dmg_ms_ = 0;
    uint64_t last_regen_ms_ = 0;     // Natural regeneration interval.
    int starve_ticks_ = 0;           // Starvation damage accumulator.
    int air_ticks_ = 300;            // Remaining underwater air.
    int fire_ticks_ = 0;             // Remaining burn time.
    bool moved_this_tick_ = false;   // Movement flag for hunger updates.
    bool sprinting_ = false;         // Client sprint state.
    bool sneaking_ = false;          // Client crouch state.

    // --- Sleep state ---
    // Stores the sleeping state and bed head coordinates.
    bool sleeping_ = false;
    int32_t sleep_x_ = 0, sleep_y_ = 0, sleep_z_ = 0;

    // --- Personal spawn ---
    // Uses the world spawn until a bed spawn is set.
    bool has_spawn_override_ = false;
    int32_t spawn_x_ = 0, spawn_y_ = 64, spawn_z_ = 0;

    // --- Nether portal state ---
    bool in_portal_ = false;      // Player is inside a portal.
    uint64_t last_portal_ms_ = 0; // Last portal travel timestamp.
    // Waits for chunks before completing a dimension switch.
    bool dimension_spawn_pending_ = false;
    uint64_t dimension_switch_ms_ = 0;
    // World to return to after Nether travel.
    World* portal_return_ = nullptr;
    // Stores the last portal pair for round-trip travel.
    bool portal_link_used_ = false;
    int32_t portal_link_ax_ = 0, portal_link_ay_ = 0, portal_link_az_ = 0;
    int32_t portal_link_bx_ = 0, portal_link_by_ = 0, portal_link_bz_ = 0;
    uint8_t portal_link_adim_ = 0, portal_link_bdim_ = 0;

    bool on_ground_fall_check_ = true; // Last client-reported ground state.
    float fall_distance_ = 0.0f;       // Accumulated fall distance.

    // Resolves a portal destination in the target world.
    bool resolvePortalDestination(World* target, double& x, double& y, double& z,
                                  PortalArrival& arrival);
    // Stores a completed portal link.
    void storePortalLink(const PortalArrival& from, uint8_t from_dim, const PortalArrival& to,
                         uint8_t to_dim);

    // Shared chunk spiral algorithm.
    static std::vector<std::pair<int32_t, int32_t>> buildChunkSpiral(int vd);

    // Chunks already sent to this player, protected by chunks_mtx_.
    std::unordered_set<int64_t> used_chunks_;
    mutable std::mutex chunks_mtx_;
    // Ensures the spawn acknowledgment triggers re-streaming only once.
    std::atomic<bool> spawn_ack_done_{false};

    // Pending chunk requests in spiral order.
    std::vector<std::pair<int32_t, int32_t>> chunk_queue_;
    mutable std::mutex queue_mtx_;

    mutable std::mutex batch_mtx_;
    std::vector<std::vector<uint8_t>> batch_unordered_;
    std::vector<std::vector<uint8_t>> batch_ordered_;
    size_t batch_bytes_ = 0;
    // Flush batches immediately when they reach 32 KB.
    static constexpr size_t BATCH_FLUSH_BYTES = 32 * 1024;

    // Ensures close() cleanup runs only once across threads.
    std::once_flag close_once_;
    // Defers player removal to the main server thread.
    std::atomic<bool> pending_removal_{false};

  public:
    // Checks whether player removal is pending.
    bool isPendingRemoval() const {
        return pending_removal_.load(std::memory_order_acquire);
    }
};
} // namespace cursestone