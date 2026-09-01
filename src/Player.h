// Player — full player session. Owns the network session, inventory,
// view distance, current chunk, gamemode, position, and a per-player
// tick. The Player class is the most complex class in AtomikPE;
// it's the bridge between the protocol layer (LoginPacket → MovePlayerPacket)
// and the world layer (Level, Chunk, Entity).
//
// Mirrors pocketmine/Player.php but in C++. Lifecycle:
//   1. RakLibInterface receives CLIENT_HANDSHAKE → Server::openSession()
//   2. LoginPacket validated → Player::handleLogin() sends PlayStatusPacket
//   3. Player::doFirstSpawn() sends StartGamePacket + PlayerListPacket
//   4. Each tick: Player::onUpdate() sends chunk updates, processes input
//   5. Disconnect: Player::close() saves inventory, sends DisconnectPacket
#pragma once
#include "main.h"                          // defines math::Vector3 alias
#include "IPlayer.h"
#include "entity/Human.h"
#include "level/Location.h"
#include "permission/PermissibleBase.h"
#include "network/AdvancedSourceInterface.h"
#include "Server.h"                        // NbtPtr = shared_ptr<mcp_tag>
#include "item/ItemStack.h"                // ItemStack for getDrops()
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <vector>
namespace atomikpe {
class Server;
class Network;
class SourceInterface;
namespace level { class Level; }
namespace network::protocol { class DataPacket; class LoginPacket; }
namespace inventory { class PlayerInventory; }

struct PlayerConfig {
    std::string username;
    std::string uuid;
    std::string xuid;             // Xbox Live id (online-mode)
    std::string client_id;        // RakNet GUID string
    std::string ip;
    uint16_t port = 0;
    int gamemode = 0;
    int view_distance = 8;
    int64_t runtime_eid = 1;       // unique runtime entity ID (allocated by Server)
};

class Player : public IPlayer, public entity::Human, public permission::PermissibleBase,
               public std::enable_shared_from_this<Player> {
public:
    Player(Server* srv, PlayerConfig cfg);
    ~Player() override;

    // --- IPlayer ---
    std::string getName() const override { return cfg_.username; }
    bool isOnline() const override { return connected_.load(std::memory_order_relaxed); }
    bool isOp() const override;
    void setOp(bool v) override;
    bool isBanned() const override;
    void setBanned(bool v) override;
    bool isWhitelisted() const override;
    void setWhitelisted(bool v) override;
    bool hasPermission(const std::string& name) const override { return PermissibleBase::hasPermission(name); }
    void recalculatePermissions() override {}

    // --- Player session lifecycle ---
    bool isConnected() const { return connected_.load(std::memory_order_relaxed); }
    bool loggedIn() const { return logged_in_.load(std::memory_order_relaxed); }
    void close(const std::string& reason = "disconnect.quitting");

    // --- Network callbacks (called from Network::dispatchSingle) ---
    void handleDataPacket(network::protocol::DataPacket& p);
    void handleLogin(network::protocol::LoginPacket& p);
    void handleMovePlayer(double x, double y, double z, float yaw, float pitch);
    void handleChat(const std::string& message);
    void handleBlockBreak(int x, int y, int z);
    void handleBlockPlace(int x, int y, int z, uint8_t face,
                          uint8_t heldItemId, uint8_t heldCount, uint16_t heldDamage);
    void handlePlayerAction(int action, int x, int y, int z, int face);

    // Spawn a dropped item entity at the given position with a small random
    // outward motion. Mirrors PMMP `Level::dropItem`. Returns the allocated
    // runtime entity ID (or 0 if the item id was 0).
    int64_t dropItem(int x, int y, int z, uint16_t itemId, uint8_t count, uint16_t damage);

    // Send the creative-inventory contents (ContainerSetContentPacket with
    // windowid=0x79). Mirrors PMMP Player.php:2301 + 1461.
    void sendCreativeInventory();

    // Send the player's main inventory + armor to the client. Called on
    // spawn and after inventory changes that the client must sync to.
    // Mirrors Nukkit's Player.sendInventory() / sendArmor().
    void sendInventory();

    // Send a single slot confirmation to the client. Mirrors Nukkit's
    // PlayerInventory.sendSlot() — after setItem(), Nukkit sends a
    // ContainerSetSlotPacket back to the client so it confirms the
    // change. Without this, MCPE 0.15.x reverts the slot visually.
    void sendSlot(int slot);

    // Save this player's data (NBT + inventory) to disk via Server.
    void saveData();

    // Returns true if this player is currently tracking the given chunk.
    // Used to broadcast particles + sounds only to nearby players.
    bool isTrackingChunk(int32_t cx, int32_t cz) const;

    // --- Server → Player packets ---
    void sendPacket(std::shared_ptr<network::protocol::DataPacket> p);
    void sendMessage(const std::string& msg);
    void kick(const std::string& reason = "disconnect.kick");
    void teleport(double x, double y, double z);

    // --- Spawning ---
    void doFirstSpawn();
    void sendChunksAroundSpawn();
    bool spawned() const { return spawned_.load(std::memory_order_relaxed); }
    int gamemode() const { return cfg_.gamemode; }
    void setGamemode(int gm);

    // --- Tick ---
    void onUpdate(uint64_t current_tick);

    // --- Inventory ---
    std::shared_ptr<inventory::PlayerInventory> getInventory() const { return inv_; }

    // --- Coordinates for chunk sending ---
    int chunkX() const { return (int)(pos_.x) >> 4; }
    int chunkZ() const { return (int)(pos_.z) >> 4; }
    int viewDistance() const { return cfg_.view_distance; }

    // --- NetworkSession accessors ---
    // Player stores a non-owning pointer to AdvancedSourceInterface (the RakLib
    // instance is owned by Network). Lifetime is bounded by Network's lifetime.
    void setSourceInterface(network::AdvancedSourceInterface* iface) { adv_iface_ = iface; }
    network::AdvancedSourceInterface* sourceInterface() const { return adv_iface_; }
    Server* server() const { return srv_; }
    const PlayerConfig& config() const { return cfg_; }
    const std::string& ip() const { return cfg_.ip; }
    uint16_t port() const { return cfg_.port; }

    // --- Player NBT data ---
    NbtPtr namedTag() const { return named_tag_; }
    void setNamedTag(NbtPtr n) { named_tag_ = std::move(n); }

    // --- Gamemode helpers (Nukkit: isSurvival/isCreative/isAdventure/isSpectator) ---
    bool isSurvival() const  { return cfg_.gamemode == 0; }
    bool isCreative() const  { return (cfg_.gamemode & 1) != 0; }
    bool isAdventure() const { return cfg_.gamemode == 2; }
    bool isSpectator() const { return cfg_.gamemode == 3; }

    // --- Player info (Nukkit: getDisplayName/getAddress/getClientId) ---
    std::string getDisplayName() const { return cfg_.username; }
    std::string getAddress() const { return cfg_.ip; }
    int64_t getClientId() const { return std::stoll(cfg_.client_id.empty() ? "0" : cfg_.client_id); }
    bool hasPlayedBefore() const { return named_tag_ != nullptr; }

    // --- Position/teleport (Nukkit: getNextPosition/getSpawn/sendPosition) ---
    math::Vector3 getNextPosition() const { return pos_; }
    math::Vector3 getSpawn() const;
    void sendPosition(double x, double y, double z, double yaw = 0, double pitch = 0, uint8_t mode = 0);

    // --- Chat/messaging (Nukkit: sendMessage/sendPopup/sendTip/sendTranslation) ---
    void sendPopup(const std::string& msg);
    void sendTip(const std::string& msg);
    void sendTranslation(const std::string& key);

    // --- Experience (Nukkit: setExperience/sendExperience) ---
    void setExperience(int exp, int level);
    void sendExperience(int exp);
    void sendExperienceLevel(int level);

    // --- Health/food (Nukkit: setHealth/getFoodData/isFoodEnabled) ---
    void setHealth(int h);
    bool isFoodEnabled() const { return cfg_.gamemode == 0; }

    // --- Chunk management (Nukkit: isChunkLoaded/isChunkLoading/unloadChunk/sendChunk) ---
    bool isChunkLoaded(int cx, int cz) const;
    bool isChunkLoading(int cx, int cz) const;
    void unloadChunk(int cx, int cz);
    void sendChunk(int cx, int cz);

    // --- Visibility (Nukkit: canSee/hidePlayer/showPlayer/spawnTo) ---
    bool canSee(Player* other) const { return other != nullptr; }
    void hidePlayer(Player* /*other*/) {}
    void showPlayer(Player* /*other*/) {}
    void spawnTo(Player* player);

    // --- Sleep (Nukkit: isSleeping/stopSleep) ---
    bool isSleeping() const { return false; }
    void stopSleep() {}

    // --- Interaction (Nukkit: canInteract/canCollideWith/resetFallDistance) ---
    bool canInteract(math::Vector3 pos, double maxDistance) const;
    bool canCollideWith(Entity* /*e*/) const { return false; }
    void resetFallDistance() {}

    // --- Flight (Nukkit: getAllowFlight/setAllowFlight/hasAutoJump/setAutoJump) ---
    bool getAllowFlight() const { return (cfg_.gamemode & 1) != 0; }
    void setAllowFlight(bool /*v*/) {}
    bool hasAutoJump() const { return true; }
    void setAutoJump(bool /*v*/) {}

    // --- Drops (Nukkit: getDrops) ---
    std::vector<atomikpe::item::ItemStack> getDrops() const;

    // --- Windows (Nukkit: getWindowId/addWindow/removeWindow) ---
    int getWindowId(void* /*inventory*/) const { return -1; }
    int addWindow(void* /*inventory*/, int /*forceId*/ = -1) { return -1; }
    void removeWindow(void* /*inventory*/) {}

    // --- Dimension (Nukkit: setDimension/teleportToNether/teleportToOverworld) ---
    void setDimension() {}
    void teleportToNether() {}
    void teleportToOverworld() {}

    // --- Settings (Nukkit: setMovementSpeed/setAttribute/sendSettings) ---
    void setMovementSpeed(float speed);
    void setAttribute(int attrId, float value);
    void sendSettings();

    // --- Killer (Nukkit: getKiller) ---
    Entity* getKiller() const { return nullptr; }

    // --- Batch packets (Nukkit: batchDataPacket/dataPacket/directDataPacket) ---
    bool batchDataPacket(const std::vector<uint8_t>& data);
    bool dataPacket(std::shared_ptr<network::protocol::DataPacket> pk) { sendPacket(pk); return true; }
    bool directDataPacket(std::shared_ptr<network::protocol::DataPacket> pk) { sendPacket(pk); return true; }

private:
    Server* srv_;
    PlayerConfig cfg_;
    network::AdvancedSourceInterface* adv_iface_ = nullptr;
    std::shared_ptr<inventory::PlayerInventory> inv_;
    std::shared_ptr<Level> level_;
    NbtPtr named_tag_;                       // player.dat contents (lazy load)
    std::atomic<bool> connected_{false};
    std::atomic<bool> logged_in_{false};
    std::atomic<bool> spawned_{false};
    uint64_t last_move_tick_ = 0;
    uint64_t last_save_tick_ = 0;

    // Chunks already sent to this player (chunkHash = (cx<<32)|cz&0xFFFFFFFF).
    // Guarded by chunks_mtx_.
    std::unordered_set<int64_t> used_chunks_;
    mutable std::mutex chunks_mtx_;

    // Pending chunk request queue (spiral order, set by sendChunksAroundSpawn).
    std::vector<std::pair<int32_t,int32_t>> chunk_queue_;
    mutable std::mutex queue_mtx_;

    // --- Thread-safe close ---
    // close() may be called from the RakLib network thread (timeout,
    // client disconnect) while the main server thread holds a shared_ptr
    // to this Player. std::call_once guarantees the cleanup body runs
    // exactly once even if two threads call close() simultaneously.
    std::once_flag close_once_;
    // When true, the main thread will remove this player from players_
    // on the next tick. Set by close() on the network thread; read+acted
    // on by Server::tick() on the main thread. This avoids destroying
    // the shared_ptr (and thus the Player) from the network thread
    // while the main thread is iterating players_.
    std::atomic<bool> pending_removal_{false};

public:
    // Returns true if close() has been called (cleanup may still be
    // in progress on another thread). Used by Server::tick() to skip
    // players that are being torn down.
    bool isPendingRemoval() const { return pending_removal_.load(std::memory_order_acquire); }
};
} // namespace
