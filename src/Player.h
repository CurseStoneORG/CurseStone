// Player — full player session. Owns the network session, inventory,
// view distance, current chunk, gamemode, position, and a per-player
// tick. The Player class is the most complex class in CenisysPE;
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
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <vector>
namespace cenisyspe {
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
};

class Player : public IPlayer, public entity::Human, public permission::PermissibleBase {
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
    void handleBlockPlace(int x, int y, int z, uint8_t face, uint8_t block_id);
    void handlePlayerAction(int action, int x, int y, int z, int face);

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
};
} // namespace
