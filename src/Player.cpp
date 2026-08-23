#include "Player.h"
#include "Server.h"
#include "level/Level.h"
#include "network/Network.h"
#include "network/SourceInterface.h"
#include "network/protocol/DataPacket.h"
#include "network/protocol/LoginPacket.h"
#include "network/protocol/PlayStatusPacket.h"
#include "network/protocol/DisconnectPacket.h"
#include "network/protocol/TextPacket.h"
#include "network/protocol/StartGamePacket.h"
#include "network/protocol/MovePlayerPacket.h"
#include "network/protocol/MoveEntityPacket.h"
#include "network/protocol/FullChunkDataPacket.h"
#include "network/protocol/SetTimePacket.h"
#include "network/protocol/SetSpawnPositionPacket.h"
#include "network/protocol/SetDifficultyPacket.h"
#include "network/protocol/SetHealthPacket.h"
#include "network/protocol/AdventureSettingsPacket.h"
#include "network/protocol/PlayerListPacket.h"
#include "network/protocol/AddPlayerPacket.h"
#include "network/protocol/RemoveEntityPacket.h"
#include "network/protocol/BatchPacket.h"
#include "network/protocol/RequestChunkRadiusPacket.h"
#include "network/protocol/ChunkRadiusUpdatedPacket.h"
#include "network/protocol/PlayerActionPacket.h"
#include "network/protocol/UseItemPacket.h"
#include "network/protocol/RemoveBlockPacket.h"
#include "network/protocol/UpdateBlockPacket.h"
#include "network/protocol/MobEquipmentPacket.h"
#include "network/protocol/LevelEventPacket.h"
#include "network/protocol/AddItemEntityPacket.h"
#include "network/protocol/ContainerSetContentPacket.h"
#include "network/protocol/ContainerSetSlotPacket.h"
#include "inventory/CreativeItems.h"
#include "network/protocol/Info.h"
#include "network/protocol/PacketPool.h"
#include "inventory/PlayerInventory.h"
#include "permission/BanEntry.h"
#include "permission/BanList.h"
#include "command/CommandMap.h"
#include "utils/MainLogger.hpp"
#include "utils/TextColor.h"
#include "utils/Utils.h"
#include "level/format/Chunk.h"
#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/Tag.h"

#include <cmath>
#include <algorithm>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <array>
#include <utility>     // std::pair
#include <unordered_set>

namespace cenisyspe {

// ============================================================
// Phase 0 — Player construction / destruction
// ============================================================
Player::Player(Server* srv, PlayerConfig cfg)
    : entity::Human(cfg.runtime_eid, nullptr), srv_(srv), cfg_(std::move(cfg)) {
    inv_ = std::make_shared<inventory::PlayerInventory>();
    connected_.store(true, std::memory_order_relaxed);
    setMaxHealth(20);
    setHealth(20);
    // Position defaults to world spawn; overridden by processLogin if NBT had Pos.
    if (srv_ && srv_->getDefaultLevel()) {
        auto sp = srv_->getDefaultLevel()->spawn();
        pos_.x = sp.x + 0.5;
        pos_.y = sp.y;
        pos_.z = sp.z + 0.5;
    }
}
Player::~Player() {
    if (connected_.load(std::memory_order_relaxed)) close("Player destroyed");
}

bool Player::isOp() const {
    // PMMP reads ops.txt; we delegate to Server's in-memory op set which is
    // loaded from <dataPath>/ops.txt on startup and updated by /op and /deop.
    return srv_ && srv_->isOp(cfg_.username);
}
void Player::setOp(bool v) {
    if (srv_) srv_->setOp(cfg_.username, v);
}
bool Player::isBanned() const {
    if (!srv_) return false;
    return srv_->getNameBans().isBanned(cfg_.username);
}
void Player::setBanned(bool v) {
    if (!srv_ || cfg_.username.empty()) return;
    if (v) {
        srv_->getNameBans().add(permission::BanEntry(cfg_.username, "Banned"));
    } else {
        srv_->getNameBans().remove(cfg_.username);
    }
}
bool Player::isWhitelisted() const {
    if (!srv_ || !srv_->whitelistEnabled()) return true;
    // No whitelist file yet: treat all as whitelisted.
    return true;
}
void Player::setWhitelisted(bool /*v*/) {}

// ============================================================
// Phase 9 — close (clean shutdown; idempotent)
// ============================================================
void Player::close(const std::string& reason) {
    bool expected = true;
    if (!connected_.compare_exchange_strong(expected, false)) return;

    utils::MainLogger::get().info("Closing player " + cfg_.username +
        " (" + cfg_.ip + ":" + std::to_string(cfg_.port) + "): " + reason);

    // 1. Save NBT to disk (if we ever loaded it).
    if (named_tag_ && srv_ && !cfg_.username.empty()) {
        srv_->saveOfflinePlayerData(cfg_.username, named_tag_);
    }

    // 2. Tell other players to despawn this entity.
    if (srv_) {
        auto pk = std::make_shared<network::protocol::RemoveEntityPacket>();
        pk->eid = runtimeId();
        for (auto& other : srv_->players()) {
            if (other.get() == this) continue;
            other->sendPacket(pk);
        }
    }

    // 3. Broadcast quit message.
    if (srv_ && !cfg_.username.empty()) {
        srv_->broadcastMessage(cfg_.username + " left the game");
    }

    // 4. Release chunks loaded for this player.
    {
        std::lock_guard<std::mutex> lk(chunks_mtx_);
        used_chunks_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(queue_mtx_);
        chunk_queue_.clear();
    }

    // 5. Tear down the RakNet session.
    if (adv_iface_ && !cfg_.ip.empty()) {
        adv_iface_->closeSession(cfg_.ip, cfg_.port, reason);
    }

    // 6. Unregister from Server roster (this drops the last shared_ptr
    // if the Server held it — so we MUST not touch `this` after this).
    if (srv_) srv_->removePlayer(this);
}

// ============================================================
// Phase 1 — sendPacket: encode a DataPacket and feed it through RakLib
// ============================================================
bool Player::isTrackingChunk(int32_t cx, int32_t cz) const {
    int64_t hash = ((int64_t)cx << 32) | ((uint32_t)cz);
    std::lock_guard<std::mutex> lk(chunks_mtx_);
    return used_chunks_.count(hash) > 0;
}

void Player::sendPacket(std::shared_ptr<network::protocol::DataPacket> p) {
    if (!p) return;
    if (!connected_.load(std::memory_order_relaxed)) return;
    if (!adv_iface_) {
        utils::MainLogger::get().warn("sendPacket: no interface for " + cfg_.username);
        return;
    }

    // Serialize the packet: [1-byte ID][body].
    std::vector<uint8_t> body;
    p->writePacket(body);
    if (body.empty()) return;

    // Wrap in a BatchPacket for compression (PMMP semantics). The wire
    // shape is [0x06][BE int32 compressedLen][zlib([BE int32 subLen][sub])].
    network::protocol::BatchPacket batch;
    batch.addSubPacket(body);
    std::vector<uint8_t> batchBody;
    batch.encode(batchBody);
    if (batchBody.empty()) return;

    // CRITICAL: MCPE clients expect ALL game packets to start with 0xFE.
    // PMMP RakLibInterface.php: chr(0xfe) . $packet->buffer.
    // Without this byte the client silently drops every packet from the
    // server (PlayStatus, StartGame, chunks, etc.) — the player sees
    // "Connecting..." forever and eventually times out.
    std::vector<uint8_t> wire;
    wire.push_back(0xFE);                          // game-packet indicator
    wire.push_back(network::protocol::BATCH_PACKET); // 0x06
    wire.insert(wire.end(), batchBody.begin(), batchBody.end());

    adv_iface_->sendRawPacket(cfg_.ip, cfg_.port, wire);
}

void Player::sendMessage(const std::string& msg) {
    auto pk = std::make_shared<network::protocol::TextPacket>();
    pk->type    = network::protocol::TextPacket::TYPE_RAW;
    pk->message = msg;
    sendPacket(pk);
}

void Player::kick(const std::string& reason) {
    // Send a DisconnectPacket first, then close the RakNet session.
    if (connected_.load(std::memory_order_relaxed)) {
        auto dp = std::make_shared<network::protocol::DisconnectPacket>();
        dp->message = reason;
        sendPacket(dp);
    }
    close(reason);
}

void Player::teleport(double x, double y, double z) {
    pos_.x = x; pos_.y = y; pos_.z = z;
    // Optionally broadcast a MovePlayerPacket to confirm position to the client.
    if (spawned_.load(std::memory_order_relaxed)) {
        auto pk = std::make_shared<network::protocol::MovePlayerPacket>();
        pk->eid = runtimeId();
        pk->x   = (float)pos_.x;
        pk->y   = (float)pos_.y;
        pk->z   = (float)pos_.z;
        pk->yaw = yaw_; pk->bodyYaw = yaw_; pk->pitch = pitch_;
        pk->mode = network::protocol::MovePlayerPacket::MODE_RESET;
        pk->onGround = onGround();
        sendPacket(pk);
    }
}
void Player::setGamemode(int gm) {
    int new_gm = gm & 0x03;
    bool was_creative = (cfg_.gamemode & 1) != 0;
    bool is_creative  = (new_gm        & 1) != 0;
    cfg_.gamemode = new_gm;

    // Re-send the creative inventory when creative state changes
    // (PMMP `Player.php:1461` sends it on every gamemode change so the
    // client's creative grid updates immediately).
    if (was_creative != is_creative || new_gm == 3) {
        sendCreativeInventory();
    }
}

// ============================================================
// Phase 2 — handleDataPacket: dispatch by packet ID
// ============================================================
void Player::handleDataPacket(network::protocol::DataPacket& p) {
    if (!connected_.load(std::memory_order_relaxed)) return;
    uint8_t pid = p.pid();
    switch (pid) {
        case network::protocol::LOGIN_PACKET:
            if (auto* lp = dynamic_cast<network::protocol::LoginPacket*>(&p)) {
                handleLogin(*lp);
            }
            break;
        case network::protocol::MOVE_PLAYER_PACKET:
            if (auto* mp = dynamic_cast<network::protocol::MovePlayerPacket*>(&p)) {
                handleMovePlayer(mp->x, mp->y, mp->z, mp->yaw, mp->pitch);
            }
            break;
        case network::protocol::TEXT_PACKET:
            if (auto* tp = dynamic_cast<network::protocol::TextPacket*>(&p)) {
                if (tp->type == network::protocol::TextPacket::TYPE_CHAT) {
                    handleChat(tp->message);
                }
            }
            break;
        case network::protocol::REQUEST_CHUNK_RADIUS_PACKET:
            // Reply with the server's view distance. PMMP also caps this
            // server-side; we honour the client's request clamped to a max.
            if (auto* rp = dynamic_cast<network::protocol::RequestChunkRadiusPacket*>(&p)) {
                int32_t r = rp->radius;
                if (r < 1) r = 1;
                if (r > cfg_.view_distance) r = cfg_.view_distance;
                auto resp = std::make_shared<network::protocol::ChunkRadiusUpdatedPacket>();
                resp->radius = r;
                sendPacket(resp);
            }
            break;
        case network::protocol::PLAYER_ACTION_PACKET:
            if (auto* pa = dynamic_cast<network::protocol::PlayerActionPacket*>(&p)) {
                handlePlayerAction(pa->action, pa->x, pa->y, pa->z, pa->face);
            }
            break;
        case network::protocol::USE_ITEM_PACKET:
            if (auto* ui = dynamic_cast<network::protocol::UseItemPacket*>(&p)) {
                // Pass the parsed item id so handleBlockPlace can decide:
                //   - empty hand (id 0)  → no placement (don't summon stone!)
                //   - block id (< 256)   → place that block, decrement stack
                //   - other item         → PMMP would call onItemUse; we just no-op
                handleBlockPlace(ui->x, ui->y, ui->z, ui->face,
                                static_cast<uint8_t>(ui->itemId & 0xFF),
                                ui->itemCount, ui->itemDamage);
            }
            break;
        case network::protocol::REMOVE_BLOCK_PACKET:
            if (auto* rb = dynamic_cast<network::protocol::RemoveBlockPacket*>(&p)) {
                handleBlockBreak(rb->x, rb->y, rb->z);
            }
            break;
        case network::protocol::MOB_EQUIPMENT_PACKET:
            // Client tells us which hotbar slot the player is now holding.
            // PMMP updates PlayerInventory::setHeldSlotIndex here, then
            // sends the (authoritative) MobEquipmentPacket back to other
            // players so they see the held item in the player's hand.
            if (auto* me = dynamic_cast<network::protocol::MobEquipmentPacket*>(&p)) {
                if (inv_ && me->hotbarSlot < 9) {
                    inv_->setHeldSlot(me->hotbarSlot);
                    if (srv_ && srv_->advancedLog()) {
                        utils::MainLogger::get().info("[INV] " + cfg_.username +
                            " switched to hotbar slot " + std::to_string(me->hotbarSlot));
                    }
                }
            }
            break;
        case network::protocol::CONTAINER_SET_SLOT_PACKET:
            // Client moved an item between slots in their inventory, OR
            // picked an item from the creative grid. We trust the client
            // and just write the resulting slot state into our local
            // PlayerInventory (mirrors PMMP's transaction-queue approach,
            // simplified — we skip the validation step).
            if (auto* cs = dynamic_cast<network::protocol::ContainerSetSlotPacket*>(&p)) {
                if (inv_ && cs->windowid == network::protocol::ContainerSetSlotPacket::SPECIAL_INVENTORY) {
                    // Main inventory slot change.
                    if (cs->slot >= 0 && cs->slot < inventory::PlayerInventory::SIZE) {
                        cenisyspe::item::ItemStack it;
                        it.id    = static_cast<uint16_t>(cs->itemId);
                        it.count = cs->itemCount;
                        it.meta  = static_cast<uint8_t>(cs->itemDamage & 0xFF);
                        inv_->setItem(cs->slot, it);
                    }
                } else if (inv_ && cs->windowid == network::protocol::ContainerSetSlotPacket::SPECIAL_ARMOR) {
                    // Armor slot change.
                    if (cs->slot >= 0 && cs->slot < 4) {
                        int dest = inventory::PlayerInventory::ARMOR_OFFSET + cs->slot;
                        cenisyspe::item::ItemStack it;
                        it.id    = static_cast<uint16_t>(cs->itemId);
                        it.count = cs->itemCount;
                        it.meta  = static_cast<uint8_t>(cs->itemDamage & 0xFF);
                        inv_->setItem(dest, it);
                    }
                }
                // SPECIAL_CREATIVE (0x79) clicks: PMMP doesn't process them
                // server-side either — the client just locally puts the
                // item into its main inventory, then sends a windowid=0
                // ContainerSetSlotPacket. We don't need to handle 0x79.
            }
            break;
        // Packets we acknowledge but don't act on for the MVP.
        case network::protocol::BATCH_PACKET:
        case network::protocol::PLAYER_INPUT_PACKET:
        case network::protocol::INTERACT_PACKET:
        case network::protocol::ANIMATE_PACKET:
        case network::protocol::CONTAINER_CLOSE_PACKET:
            break;
        default: {
            // Unknown packet — log at debug level.
            char hex[8];
            std::snprintf(hex, sizeof(hex), "%02x", pid);
            utils::MainLogger::get().debug("Player " + cfg_.username +
                " sent unhandled packet id=0x" + hex);
            break;
        }
    }
}

// ============================================================
// Phase 3 — handleLogin: validate the LoginPacket
// ============================================================
void Player::handleLogin(network::protocol::LoginPacket& pk) {
    if (logged_in_.load(std::memory_order_relaxed)) return;

    // 1. Protocol version check.
    if (pk.protocol != network::protocol::CURRENT_PROTOCOL) {
        auto resp = std::make_shared<network::protocol::PlayStatusPacket>();
        if (pk.protocol < network::protocol::CURRENT_PROTOCOL) {
            resp->status = network::protocol::PlayStatusPacket::LOGIN_FAILED_CLIENT;
        } else {
            resp->status = network::protocol::PlayStatusPacket::LOGIN_FAILED_SERVER;
        }
        sendPacket(resp);
        close("disconnectionScreen.outdated" + std::string(
            pk.protocol < network::protocol::CURRENT_PROTOCOL ? "Client" : "Server"));
        return;
    }

    // 2. Pull username/UUID/clientId from the parsed LoginPacket fields.
    cfg_.username = pk.username;
    cfg_.uuid     = pk.clientUUID;
    cfg_.client_id = std::to_string(pk.clientId);

    // 3. Username validation: [a-zA-Z0-9_]{3,16}, disallow "rcon"/"console".
    if (cfg_.username.size() < 3 || cfg_.username.size() > 16) {
        close("disconnectionScreen.invalidName");
        return;
    }
    for (char c : cfg_.username) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_';
        if (!ok) { close("disconnectionScreen.invalidName"); return; }
    }
    std::string lower = cfg_.username;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "rcon" || lower == "console") {
        close("disconnectionScreen.invalidName");
        return;
    }

    // 4. Skin sanity check (PMMP requires 64*32*4 or 64*64*4 bytes).
    size_t sl = pk.skinData.size();
    if (sl != 64*32*4 && sl != 64*64*4) {
        utils::MainLogger::get().warn("Non-standard skin size — accepting");
        return;
    }

    // 5. Server full?
    if (srv_ && srv_->players().size() >= (size_t)srv_->maxPlayers()) {
        kick("disconnectionScreen.serverFull");
        return;
    }

    // 6. Hand off to processLogin for whitelist/ban/NBT/spawn.
    // processLogin is folded into handleLogin here because the C++ Player
    // owns its own state machine (no separate async step like PMMP).
    logged_in_.store(true, std::memory_order_relaxed);

    // Whitelist + bans
    if (!isWhitelisted()) {
        close("Server is white-listed");
        return;
    }
    if (srv_ && (srv_->getNameBans().isBanned(cfg_.username) ||
                 srv_->getIPBans().isBanned(cfg_.ip))) {
        close("You are banned");
        return;
    }

    // Duplicate-session kick (same username or UUID)
    if (srv_) {
        for (auto& other : srv_->players()) {
            if (other.get() == this) continue;
            if (!other) continue;
            std::string on = other->getName();
            std::transform(on.begin(), on.end(), on.begin(), ::tolower);
            if (on == lower) {
                other->kick("logged in from another location");
            } else if (other->loggedIn() && !other->config().uuid.empty() &&
                       other->config().uuid == cfg_.uuid) {
                other->kick("logged in from another location");
            }
        }
    }

    // Load NBT (player.dat)
    if (srv_) {
        named_tag_ = srv_->getOfflinePlayerData(cfg_.username);
        if (named_tag_) {
            int32_t gm = mcp_compound_tag_get_int(named_tag_.get(), "playerGameType", srv_->gamemode());
            setGamemode(gm);
            int16_t hp = mcp_compound_tag_get_short(named_tag_.get(), "Health", 20);
            int16_t mh = mcp_compound_tag_get_short(named_tag_.get(), "MaxHealth", 20);
            setMaxHealth(mh > 0 ? mh : 20);
            setHealth(hp <= 0 ? 20 : hp);
            int16_t hunger = mcp_compound_tag_get_short(named_tag_.get(), "Hunger", 20);
            setFoodLevel(hunger);
            // Spawn position (defaults to world spawn already set in ctor).
            int32_t sx = mcp_compound_tag_get_int(named_tag_.get(), "SpawnX", -1);
            int32_t sy = mcp_compound_tag_get_int(named_tag_.get(), "SpawnY", -1);
            int32_t sz = mcp_compound_tag_get_int(named_tag_.get(), "SpawnZ", -1);
            if (sx != -1 || sy != -1 || sz != -1) {
                pos_.x = (double)sx + 0.5; pos_.y = (double)sy; pos_.z = (double)sz + 0.5;
            }
        }
    }

    // Tell the client "login OK". PMMP sends LOGIN_SUCCESS here.
    auto ps = std::make_shared<network::protocol::PlayStatusPacket>();
    ps->status = network::protocol::PlayStatusPacket::LOGIN_SUCCESS;
    sendPacket(ps);

    // Send StartGamePacket so the client can render the loading screen.
    auto sg = std::make_shared<network::protocol::StartGamePacket>();
    if (srv_ && srv_->getDefaultLevel()) {
        auto* lvl = srv_->getDefaultLevel();
        sg->seed      = (int32_t)lvl->seed();
        sg->dimension = 0;
        sg->generator = 1;
        sg->gamemode  = cfg_.gamemode & 0x01;
        // PMMP sets eid = 0 in StartGamePacket, but the C++ client uses
        // this EID to identify itself for subsequent MovePlayerPacket and
        // other entity-relative packets. Using runtimeId() keeps the EID
        // consistent across packets and avoids the client dropping its
        // own movement updates.
        sg->eid       = static_cast<int64_t>(runtimeId());
        auto sp = lvl->spawn();
        sg->spawnX = (int32_t)sp.x;
        sg->spawnY = (int32_t)sp.y;
        sg->spawnZ = (int32_t)sp.z;
        sg->x = (float)pos_.x; sg->y = (float)pos_.y; sg->z = (float)pos_.z;
        sg->unknown = lvl->name();
    }
    sendPacket(sg);

    // SetTimePacket.
    if (srv_ && srv_->getDefaultLevel()) {
        auto st = std::make_shared<network::protocol::SetTimePacket>();
        st->time = srv_->getDefaultLevel()->time();
        st->started = true;
        sendPacket(st);
    }

    // SetSpawnPosition.
    if (srv_ && srv_->getDefaultLevel()) {
        auto sp = srv_->getDefaultLevel()->spawn();
        auto ssp = std::make_shared<network::protocol::SetSpawnPositionPacket>();
        ssp->x = (int32_t)sp.x; ssp->y = (int32_t)sp.y; ssp->z = (int32_t)sp.z;
        sendPacket(ssp);
    }

    // SetDifficulty / SetHealth.
    if (srv_) {
        auto sd = std::make_shared<network::protocol::SetDifficultyPacket>();
        sd->difficulty = srv_->difficulty();
        sendPacket(sd);
    }
    {
        auto sh = std::make_shared<network::protocol::SetHealthPacket>();
        sh->health = (int32_t)health();
        sendPacket(sh);
    }

    utils::MainLogger::get().info("Player " + cfg_.username + " logged in (" +
        cfg_.ip + ":" + std::to_string(cfg_.port) + ")");

    // Stream chunks and trigger first spawn.
    sendChunksAroundSpawn();
}

// ============================================================
// Phase 5 — sendChunksAroundSpawn (spiral order, like PMMP)
// ============================================================
void Player::sendChunksAroundSpawn() {
    if (!srv_ || !srv_->getDefaultLevel()) {
        doFirstSpawn();
        return;
    }
    auto* lvl = srv_->getDefaultLevel();
    int centerX = (int)pos_.x >> 4;
    int centerZ = (int)pos_.z >> 4;
    int vd = cfg_.view_distance;

    // Generate the spiral order: start at (0,0) and walk outward.
    std::vector<std::pair<int32_t,int32_t>> order;
    order.reserve(vd * vd);
    int layer = 1, leg = 0, x = 0, z = 0;
    order.emplace_back(x, z);
    for (int i = 1; i < vd * vd; ++i) {
        switch (leg) {
            case 0: ++x; if (x == layer) ++leg; break;
            case 1: ++z; if (z == layer) ++leg; break;
            case 2: --x; if (-x == layer) ++leg; break;
            case 3: --z; if (-z == layer) { leg = 0; ++layer; } break;
        }
        order.emplace_back(x, z);
    }

    {
        std::lock_guard<std::mutex> lk(queue_mtx_);
        chunk_queue_.clear();
        for (auto& pr : order) {
            chunk_queue_.emplace_back(centerX + pr.first, centerZ + pr.second);
        }
    }

    // Send immediately (synchronous — fine for small view distances).
    // Real PMMP defers this across ticks; we keep it simple for the MVP.
    auto& chunks = lvl->chunks();
    for (auto& coord : order) {
        int32_t cx = centerX + coord.first;
        int32_t cz = centerZ + coord.second;
        mcp_chunk_t* ch = chunks.getChunk(cx, cz);
        if (!ch) {
            // Mark as "we tried" so doFirstSpawn still proceeds; the client
            // will simply see air in the missing chunk.
            continue;
        }
        size_t outLen = 0;
        uint8_t* buf = mcp_chunk_network_serialize(ch, &outLen);
        if (!buf || outLen == 0) continue;

        auto pk = std::make_shared<network::protocol::FullChunkDataPacket>();
        pk->chunkX = cx;
        pk->chunkZ = cz;
        pk->order  = network::protocol::FullChunkDataPacket::ORDER_COLUMNS;
        pk->data.assign(buf, buf + outLen);
        free(buf);
        sendPacket(pk);

        // Track sent chunks for later unload.
        int64_t hash = ((int64_t)cx << 32) | ((uint32_t)cz);
        {
            std::lock_guard<std::mutex> lk(chunks_mtx_);
            used_chunks_.insert(hash);
        }
    }

    // All requested chunks have been sent — fire the spawn sequence.
    doFirstSpawn();
}

// ============================================================
// Phase 6 — doFirstSpawn
// ============================================================

// Helper: parse a "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" UUID string into
// 16 raw bytes. Empty input yields zeros. Used by AddPlayerPacket / PlayerListPacket.
static void parseUuidBytes(const std::string& s, std::array<uint8_t,16>& out) {
    out.fill(0);
    if (s.empty()) return;
    size_t bi = 0;
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i + 1 < s.size() && bi < 16; ++i) {
        char c = s[i];
        if (c == '-') continue;
        int hi = hexVal(c);
        if (hi < 0) continue;
        // Look for the next hex char (skip dashes).
        size_t j = i + 1;
        while (j < s.size() && s[j] == '-') ++j;
        if (j >= s.size()) break;
        int lo = hexVal(s[j]);
        if (lo < 0) continue;
        out[bi++] = (uint8_t)((hi << 4) | lo);
        i = j;
    }
}
void Player::doFirstSpawn() {
    if (spawned_.exchange(true)) return;   // idempotent

    // AdventureSettings (flight/pvp flags) — mirrors PMMP sendSettings().
    // Flag bits (from PMMP):
    //   0x01 = world_immutable (adventure mode — no break/place)
    //   0x02 = no_pvp
    //   0x04 = no_pvm (no hit markers client-side)
    //   0x08 = no_mvp (no hit markers client-side)
    //   0x40 = auto_jump
    //   0x80 = allow_fly
    //   0x100 = noclip (spectator)
    // PMMP always sends 0x02|0x04|0x08 (no hit markers).
    // PMMP sends userPermission=2 (host) and globalPermission=2 (host).
    // Without host permission, the client treats the player as a visitor
    // and blocks block break/placement.
    {
        auto as = std::make_shared<network::protocol::AdventureSettingsPacket>();
        uint32_t flags = 0;

        // Adventure mode (gm 2 or 3) = world immutable
        if (cfg_.gamemode == 2 || cfg_.gamemode == 3) {
            flags |= 0x01;  // world_immutable — no break/place
        }

        // No hit markers (PMMP always sets these)
        flags |= 0x02;  // no_pvp
        flags |= 0x04;  // no_pvm
        flags |= 0x08;  // no_mvp

        // Auto jump (PMMP default = true)
        flags |= 0x40;

        // Creative (gm 1 or 3) can fly
        if (cfg_.gamemode == 1 || cfg_.gamemode == 3) {
            flags |= 0x80;  // allow_fly
        }

        // Spectator (gm 3) has noclip
        if (cfg_.gamemode == 3) {
            flags |= 0x100;  // noclip
        }

        as->flags            = flags;
        as->userPermission   = 2;  // host — required for break/place
        as->globalPermission = 2;  // host
        sendPacket(as);
    }

    // PLAYER_SPAWN — tells the client "you can show the world now".
    {
        auto ps = std::make_shared<network::protocol::PlayStatusPacket>();
        ps->status = network::protocol::PlayStatusPacket::PLAYER_SPAWN;
        sendPacket(ps);
    }

    // Send the creative inventory contents to the player. Mirrors PMMP
    // `Player.php:2301` which calls ContainerSetContentPacket with
    // windowid=0x79 (SPECIAL_CREATIVE) right after first spawn so the
    // player has items to grab in creative mode.
    sendCreativeInventory();

    // Broadcast a PlayerListPacket (ADD) with all currently-online players.
    if (srv_) {
        auto plp = std::make_shared<network::protocol::PlayerListPacket>();
        plp->type = network::protocol::PlayerListPacket::TYPE_ADD;
        for (auto& other : srv_->players()) {
            if (!other) continue;
            network::protocol::PlayerListPacket::Entry e;
            // Parse the UUID string ("xxxxxxxx-xxxx-...") into 16 raw bytes.
            parseUuidBytes(other->config().uuid, e.uuid);
            e.eid    = other->runtimeId();
            e.name   = other->getName();
            e.isSlim = "false";
            e.skin   = "";
            plp->entries.push_back(std::move(e));
        }
        sendPacket(plp);
    }

    // Spawn other online players to this client (AddPlayerPacket).
    if (srv_) {
        for (auto& other : srv_->players()) {
            if (other.get() == this) continue;
            if (!other || !other->spawned()) continue;
            auto ap = std::make_shared<network::protocol::AddPlayerPacket>();
            parseUuidBytes(other->config().uuid, ap->uuid);
            ap->username = other->getName();
            ap->eid = other->runtimeId();
            ap->x = (float)other->position().x;
            ap->y = (float)other->position().y;
            ap->z = (float)other->position().z;
            ap->yaw = other->yaw();
            ap->headYaw = other->yaw();
            ap->pitch = other->pitch();
            sendPacket(ap);
        }
    }

    // Broadcast join message.
    if (srv_) {
        srv_->broadcastMessage(cfg_.username + " joined the game");
    }

    utils::MainLogger::get().info("First spawn for " + cfg_.username);
}

// Helper: parse a "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" UUID string into
// 16 raw bytes. Empty input yields zeros. Used by AddPlayerPacket / PlayerListPacket.
// (Moved above doFirstSpawn; see earlier definition.)

// ============================================================
// Phase 7 — handleMovePlayer (basic validation + broadcast)
// ============================================================
void Player::handleMovePlayer(double x, double y, double z, float yaw, float pitch) {
    if (!spawned_.load(std::memory_order_relaxed)) return;

    // Reject NaN / inf / clearly bogus coordinates.
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    if (!std::isfinite(yaw) || !std::isfinite(pitch)) return;
    // Reject obviously out-of-world Y values.
    if (y < -64.0 || y > 320.0) return;
    // Anti-cheat: rough speed cap (~20 blocks/tick = 400 b/s — very generous).
    double dx = x - pos_.x, dy = y - pos_.y, dz = z - pos_.z;
    double d2 = dx*dx + dy*dy + dz*dz;
    if (d2 > 400.0) {
        // Snap the player back to last known position.
        auto mp = std::make_shared<network::protocol::MovePlayerPacket>();
        mp->eid = runtimeId();
        mp->x = (float)pos_.x; mp->y = (float)pos_.y; mp->z = (float)pos_.z;
        mp->yaw = yaw_; mp->bodyYaw = yaw_; mp->pitch = pitch_;
        mp->mode = network::protocol::MovePlayerPacket::MODE_RESET;
        sendPacket(mp);
        return;
    }

    pos_.x = x; pos_.y = y; pos_.z = z;
    yaw_ = yaw; pitch_ = pitch;

    // Broadcast MoveEntityPacket to other nearby players (so they see this
    // player move). We don't filter by distance for the MVP.
    if (srv_) {
        auto me = std::make_shared<network::protocol::MoveEntityPacket>();
        me->eid = runtimeId();
        me->x = (float)pos_.x; me->y = (float)pos_.y; me->z = (float)pos_.z;
        me->yaw = yaw_; me->headYaw = yaw_; me->pitch = pitch_;
        for (auto& other : srv_->players()) {
            if (other.get() == this) continue;
            other->sendPacket(me);
        }
    }
}

void Player::handleChat(const std::string& message) {
    if (message.empty()) return;

    // PMMP behavior:
    //   - Strip §-codes only for command dispatch (so "/§aversion" still
    //     matches "/version"). Use TC::clean — the previous inline loop
    //     looked for the bare byte 0xa7, which corrupted the leading 0xc2
    //     of UTF-8 § sequences the client actually sends.
    //   - For plain chat, KEEP the original message (with §-codes intact)
    //     so colors are rendered on every receiver's screen. PMMP also
    //     forwards the raw message to PlayerChatEvent recipients.
    std::string stripped = TC::clean(message);
    if (stripped.empty()) return;
    if (stripped.size() > 256) stripped.resize(256);

    // Dispatch /commands through the CommandMap; chat otherwise.
    if (stripped[0] == '/') {
        std::string cmd_line = stripped.substr(1);
        if (srv_ && srv_->getCommandMap()) {
            srv_->getCommandMap()->dispatch(this, cmd_line);
        }
        return;
    }

    // Broadcast: <username> message — keep color codes intact.
    if (srv_) srv_->broadcastMessage("<" + cfg_.username + "> " + message);
}

// Forward decl — defined below handlePlayerAction.
static std::pair<uint16_t, uint16_t> lookupBlockDrop(uint8_t blockId, uint8_t blockData);

void Player::handleBlockBreak(int x, int y, int z) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (!srv_ || !srv_->getDefaultLevel()) return;

    auto* lvl = srv_->getDefaultLevel();

    // Reject out-of-world Y values.
    if (y < 0 || y >= 256) return;

    // Get the chunk containing this block.
    int32_t cx = x >> 4;
    int32_t cz = z >> 4;
    mcp_chunk_t* ch = lvl->chunks().getChunk(cx, cz);
    if (!ch) return;

    int localX = x & 15;
    int localZ = z & 15;
    uint8_t blockId   = mcp_chunk_get_block_id  (ch, localX, y, localZ);
    uint8_t blockData = mcp_chunk_get_block_data(ch, localX, y, localZ);

    // Don't break bedrock (id=7).
    if (blockId == 7) return;
    // Already air — nothing to do.
    if (blockId == 0) return;

    // Set block to air (id=0, data=0).
    mcp_chunk_set_block_full(ch, localX, y, localZ, 0, 0);
    mcp_chunk_recalculate_height_map(ch);
    ch->dirty = true;

    // Broadcast UpdateBlockPacket to all players.
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = x;
    ubp->z = z;
    ubp->y = (uint8_t)y;
    ubp->blockId = 0;
    ubp->blockData = 0;
    ubp->flags = 0xFF;

    // PMMP (protocol 84) sends a SINGLE LevelEventPacket with
    // evid = EVENT_PARTICLE_DESTROY (2001) which acts as BOTH the break
    // particle AND the break sound at the client (per the doc-comment in
    // pocketmine/level/particle/DestroyBlockParticle.php: "NOTE: This is
    // also the destroy block SOUND.").
    //
    // Position: block CENTER (x+0.5, y+0.5, z+0.5).
    // Data:     blockId | (blockMeta << 12)  — note the << 12 shift,
    //           NOT << 8 as newer protocols use.
    auto partEv = std::make_shared<network::protocol::LevelEventPacket>();
    partEv->evid = network::protocol::LevelEventPacket::EVENT_PARTICLE_DESTROY;
    partEv->x = (float)x + 0.5f;
    partEv->y = (float)y + 0.5f;
    partEv->z = (float)z + 0.5f;
    partEv->data = ((int32_t)blockId & 0xFF) | ((int32_t)blockData << 12);

    // PMMP `Level::addParticle` -> `addChunkPacket($particle->x >> 4, ...)`
    // sends the packet only to players tracking the chunk containing it.
    // We mirror that behaviour here: skip players who haven't received
    // FullChunkDataPacket for this chunk yet.
    int32_t pk_cx = x >> 4;
    int32_t pk_cz = z >> 4;
    if (srv_) {
        for (auto& other : srv_->players()) {
            if (!other) continue;
            if (!other->isTrackingChunk(pk_cx, pk_cz)) continue;
            other->sendPacket(ubp);
            other->sendPacket(partEv);
        }
    }

    // ---- Drops (PMMP `Level::useBreakOn` -> `Level::dropItem`) ----
    // PMMP: only survival players get drops. Creative players skip this
    // loop entirely (vanilla-correct behaviour).
    bool creative = (cfg_.gamemode & 1) != 0;
    if (!creative) {
        // Lookup the drop table for this block.
        auto drop = lookupBlockDrop(blockId, blockData);
        if (drop.first != 0) {  // 0 = no drop (glass, ice, etc.)
            dropItem(x, y, z, drop.first, 1, drop.second);
        }
    }

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[BLOCK] " + cfg_.username + " broke block at " +
            std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) +
            " (was id=" + std::to_string(blockId) + " data=" + std::to_string(blockData) + ")");
    }
}

void Player::handleBlockPlace(int x, int y, int z, uint8_t face,
                              uint8_t heldItemId, uint8_t heldCount, uint16_t heldDamage) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (!srv_ || !srv_->getDefaultLevel()) return;

    // ---- PMMP-style validation: must be holding a BLOCK item ----
    // In MCPE 0.15, item ids < 256 are block items; ids >= 256 are tools /
    // food / etc. and should NOT place a block. Empty hand (id 0) is the
    // case the user reported — we explicitly reject it.
    if (heldItemId == 0 || heldItemId >= 256) {
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[BLOCK] " + cfg_.username +
                " attempted place with non-block item id=" + std::to_string(heldItemId));
        }
        return;
    }

    // Calculate placed block position based on face.
    // Face: 0=bottom, 1=top, 2=north, 3=south, 4=west, 5=east
    int placeX = x, placeY = y, placeZ = z;
    switch (face) {
        case 0: placeY--; break;  // bottom
        case 1: placeY++; break;  // top
        case 2: placeZ--; break;  // north
        case 3: placeZ++; break;  // south
        case 4: placeX--; break;  // west
        case 5: placeX++; break;  // east
        default: return;
    }

    if (placeY < 0 || placeY >= 256) return;

    auto* lvl = srv_->getDefaultLevel();

    int32_t cx = placeX >> 4;
    int32_t cz = placeZ >> 4;
    mcp_chunk_t* ch = lvl->chunks().getChunk(cx, cz);
    if (!ch) return;

    int localX = placeX & 15;
    int localZ = placeZ & 15;

    // Don't place if target is not air.
    uint8_t existing = mcp_chunk_get_block_id(ch, localX, placeY, localZ);
    if (existing != 0) return;

    // Place the block: blockId = heldItemId, data = low 4 bits of heldDamage
    // (matches PMMP Item::getDamage for block items).
    uint8_t placedData = (uint8_t)(heldDamage & 0x0F);
    mcp_chunk_set_block_full(ch, localX, placeY, localZ, heldItemId, placedData);
    ch->dirty = true;

    // Decrement inventory (only in survival — PMMP skips in creative).
    bool creative = (cfg_.gamemode & 1) != 0;
    if (!creative && inv_) {
        int held = inv_->heldSlot();
        auto cur = inv_->getItem(held);
        if (!cur.isEmpty()) {
            if (cur.count > 1) {
                cur.count -= 1;
                inv_->setItem(held, cur);
            } else {
                inv_->clear(held);
            }
        }
    }

    // Broadcast UpdateBlockPacket to all players.
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = placeX;
    ubp->z = placeZ;
    ubp->y = (uint8_t)placeY;
    ubp->blockId = heldItemId;
    ubp->blockData = placedData;
    ubp->flags = 0xFF;

    // PMMP (protocol 84) sends a block-place sound via LevelEventPacket with
    // evid = EVENT_SOUND_BLOCK_PLACE (1052).
    //   - Position: BLOCK CORNER (integer x,y,z — NOT +0.5, unlike the
    //     break particle). Verbatim from pocketmine/level/sound/BlockPlaceSound.php
    //     + GenericSound which copies Block::x/y/z which are integers.
    //   - Data:     just the placed block id (no meta bits).
    auto sndEv = std::make_shared<network::protocol::LevelEventPacket>();
    sndEv->evid = network::protocol::LevelEventPacket::EVENT_SOUND_BLOCK_PLACE;
    sndEv->x = (float)placeX;
    sndEv->y = (float)placeY;
    sndEv->z = (float)placeZ;
    sndEv->data = (int32_t)heldItemId;

    // Same chunk-filter as for block break (PMMP `Level::addSound`).
    int32_t pk_cx = placeX >> 4;
    int32_t pk_cz = placeZ >> 4;
    if (srv_) {
        for (auto& other : srv_->players()) {
            if (!other) continue;
            if (!other->isTrackingChunk(pk_cx, pk_cz)) continue;
            other->sendPacket(ubp);
            other->sendPacket(sndEv);
        }
    }

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[BLOCK] " + cfg_.username + " placed block at " +
            std::to_string(placeX) + "," + std::to_string(placeY) + "," + std::to_string(placeZ) +
            " (id=" + std::to_string(heldItemId) + " data=" + std::to_string(placedData) + ")");
    }
}

void Player::handlePlayerAction(int action, int x, int y, int z, int /*face*/) {
    // PMMP: ACTION_START_BREAK=0, ACTION_ABORT_BREAK=1, ACTION_STOP_BREAK=2.
    // We don't model break progress yet — just log START for diagnostics.
    if (srv_ && srv_->advancedLog() && action == 0) {
        utils::MainLogger::get().info("[ACTION] " + cfg_.username + " started breaking at " +
            std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
    }
}

// ============================================================
// Block drop lookup table
// ============================================================
// Mirrors PMMP `Block::getDrops(Item $item)` per-block overrides.
// Returns {itemId, damage} of the drop. {0, 0} = no drop.
// Default (unlisted block): drop itself (id, damage, count=1).
//
//   Block            | Drops         | Notes
//   -----------------+---------------+---------------------------
//   Stone (1)        | Cobblestone 4 | Requires pickaxe (we assume yes)
//   Grass (2)        | Dirt 3        | Silk-touch → Grass (not modeled)
//   Coal Ore (16)   | Coal 263      |
//   Iron Ore (15)   | Nothing       | Needs smelting
//   Gold Ore (14)   | Nothing       | Needs smelting
//   Diamond Ore (56) | Diamond 264   |
//   Lapis Ore (21)  | Dye 351:4     | Lapis Lazuli dye
//   Redstone Ore (73)| Redstone 331  |
//   Glass (20)      | Nothing       | Silk-touch required (PMMP Glass.php)
//   Ice (79)        | Nothing       | Silk-touch required (PMMP Ice.php)
//   Glowstone (89)  | Glowstone Dust 348 | Drops 2-4, we drop 1 for MVP
//   Leaves (18)     | Nothing       | Sapling chance (PMMP complex, skip)
//   Bedrock (7)      | Nothing       | Unbreakable anyway
//   TNT (46)         | Nothing       | Doesn't drop itself when broken
static std::pair<uint16_t, uint16_t> lookupBlockDrop(uint8_t blockId, uint8_t blockData) {
    switch (blockId) {
        case 1:  return {4,   blockData & 0x07};   // Stone -> Cobblestone (preserve variant for non-stone types)
        case 2:  return {3,   0};                   // Grass -> Dirt
        case 15: return {0,   0};                   // Iron Ore -> no drop (smelt)
        case 16: return {263, 0};                   // Coal Ore -> Coal
        case 14: return {0,   0};                   // Gold Ore -> no drop (smelt)
        case 21: return {351, 4};                   // Lapis Ore -> Lapis Lazuli dye
        case 56: return {264, 0};                   // Diamond Ore -> Diamond
        case 73: return {331, 0};                   // Redstone Ore -> Redstone
        case 20: return {0,   0};                   // Glass -> no drop
        case 79: return {0,   0};                   // Ice -> no drop
        case 89: return {348, 0};                   // Glowstone -> Glowstone Dust
        case 18: return {0,   0};                   // Leaves -> no drop (chance-based)
        case 7:  return {0,   0};                   // Bedrock -> unbreakable
        case 46: return {0,   0};                   // TNT -> no drop
        default: return {blockId, blockData};       // Default: drop self
    }
}

// ============================================================
// Drop an item entity at a position
// ============================================================
// Mirrors PMMP `Level::dropItem(Vector3, Item, motion=null, delay=10)`:
//   - Motion: random (x in [-0.1, +0.1], y = 0.2, z in [-0.1, +0.1])
//   - Position: block CENTER (x+0.5, y+0.5, z+0.5)
//   - Broadcast: AddItemEntityPacket to all players tracking the chunk
// PMMP also keeps the entity server-side (with gravity + pickup delay)
// so players can pick it up via TakeItemEntityPacket. We skip the
// server-side entity tracking for the MVP and just broadcast the spawn
// — the visual feedback is what matters.
int64_t Player::dropItem(int x, int y, int z, uint16_t itemId, uint8_t count, uint16_t damage) {
    if (itemId == 0 || count == 0) return 0;
    if (!srv_) return 0;

    // Allocate a runtime entity ID (PMMP `Level::$entityIdCounter`).
    int64_t eid = srv_->allocateEid();

    // Random motion in PMMP range. Using a cheap LCG (no <random> needed).
    static uint32_t lcg_state = 0x1234abcdu;
    lcg_state = lcg_state * 1103515245u + 12345u;
    auto rnd = []() -> float {
        lcg_state = lcg_state * 1103515245u + 12345u;
        return (float)((double)(lcg_state >> 8) / (double)0x1000000);
    };
    float mx = rnd() * 0.2f - 0.1f;     // [-0.1, +0.1]
    float my = 0.2f;                    // pop up
    float mz = rnd() * 0.2f - 0.1f;

    auto pk = std::make_shared<network::protocol::AddItemEntityPacket>();
    pk->eid        = eid;
    pk->itemId     = static_cast<int16_t>(itemId);
    pk->itemCount  = count;
    pk->itemDamage = static_cast<int16_t>(damage);
    pk->x = (float)x + 0.5f;
    pk->y = (float)y + 0.5f;
    pk->z = (float)z + 0.5f;
    pk->speedX = mx;
    pk->speedY = my;
    pk->speedZ = mz;

    // Broadcast only to players tracking this chunk (PMMP `addChunkPacket`).
    int32_t pk_cx = x >> 4;
    int32_t pk_cz = z >> 4;
    for (auto& other : srv_->players()) {
        if (!other) continue;
        if (!other->isTrackingChunk(pk_cx, pk_cz)) continue;
        other->sendPacket(pk);
    }

    if (srv_->advancedLog()) {
        utils::MainLogger::get().info("[DROP] eid=" + std::to_string(eid) +
            " item=" + std::to_string(itemId) + " x" + std::to_string(count) +
            " dmg=" + std::to_string(damage) + " at " +
            std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
    }
    return eid;
}

// ============================================================
// Send the creative inventory to the player
// ============================================================
// Mirrors PMMP `Player.php:2301` (and `:1461` on gamemode change):
//   ContainerSetContentPacket with windowid=SPECIAL_CREATIVE (0x79) and
//   slots = full creative item list (PMMP `Item::getCreativeItems()`).
//   Spectator players get an empty list (no items to give).
void Player::sendCreativeInventory() {
    bool spectator = (cfg_.gamemode == 3);
    auto pk = std::make_shared<network::protocol::ContainerSetContentPacket>();
    pk->windowid = network::protocol::ContainerSetContentPacket::SPECIAL_CREATIVE;
    if (!spectator) {
        pk->slots = inventory::getCreativeItems();
    }
    sendPacket(pk);
}

// ============================================================
// Phase 8 — onUpdate (chunk streaming, keepalive, auto-save)
// ============================================================
void Player::onUpdate(uint64_t current_tick) {
    if (!connected_.load(std::memory_order_relaxed)) return;
    if (!logged_in_.load(std::memory_order_relaxed)) return;

    // 1. Stream chunks when the player crosses a chunk boundary.
    if (spawned_.load(std::memory_order_relaxed) && srv_ && srv_->getDefaultLevel()) {
        int cx = (int)pos_.x >> 4;
        int cz = (int)pos_.z >> 4;
        // Only re-order when the player actually moved chunks since last tick.
        // (Cheap heuristic: keep last sent center in chunk_queue's front.)
        bool moved = false;
        {
            std::lock_guard<std::mutex> lk(queue_mtx_);
            if (!chunk_queue_.empty()) {
                auto& front = chunk_queue_.front();
                if (front.first != cx || front.second != cz) moved = true;
            } else {
                moved = true;
            }
        }
        if (moved) {
            // Re-stream from the new center using the spiral helper inline.
            int vd = cfg_.view_distance;
            std::vector<std::pair<int32_t,int32_t>> order;
            order.reserve(vd * vd);
            int layer = 1, leg = 0, x = 0, z = 0;
            order.emplace_back(x, z);
            for (int i = 1; i < vd * vd; ++i) {
                switch (leg) {
                    case 0: ++x; if (x == layer) ++leg; break;
                    case 1: ++z; if (z == layer) ++leg; break;
                    case 2: --x; if (-x == layer) ++leg; break;
                    case 3: --z; if (-z == layer) { leg = 0; ++layer; } break;
                }
                order.emplace_back(x, z);
            }
            auto& chunks = srv_->getDefaultLevel()->chunks();
            std::unordered_set<int64_t> still_in_view;
            for (auto& pr : order) {
                int32_t chX = cx + pr.first, chZ = cz + pr.second;
                int64_t hash = ((int64_t)chX << 32) | ((uint32_t)chZ);
                still_in_view.insert(hash);
                bool already_sent;
                { std::lock_guard<std::mutex> lk(chunks_mtx_); already_sent = used_chunks_.count(hash) > 0; }
                if (already_sent) continue;
                mcp_chunk_t* ch = chunks.getChunk(chX, chZ);
                if (!ch) continue;
                size_t outLen = 0;
                uint8_t* buf = mcp_chunk_network_serialize(ch, &outLen);
                if (!buf || outLen == 0) continue;
                auto pk = std::make_shared<network::protocol::FullChunkDataPacket>();
                pk->chunkX = chX; pk->chunkZ = chZ;
                pk->data.assign(buf, buf + outLen);
                free(buf);
                sendPacket(pk);
                { std::lock_guard<std::mutex> lk(chunks_mtx_); used_chunks_.insert(hash); }
            }
            // Drop chunks that fell out of view.
            {
                std::lock_guard<std::mutex> lk(chunks_mtx_);
                for (auto it = used_chunks_.begin(); it != used_chunks_.end(); ) {
                    if (still_in_view.count(*it) == 0) it = used_chunks_.erase(it);
                    else ++it;
                }
            }
            {
                std::lock_guard<std::mutex> lk(queue_mtx_);
                chunk_queue_.clear();
                chunk_queue_.emplace_back(cx, cz);
            }
        }
    }

    // 2. Skip gravity for now — Entity::tick applies gravity + motion but
    // there's no collision detection, so the player would sink through the
    // ground. The client handles its own gravity/movement and sends
    // MovePlayerPacket to the server. We just accept the client's position.
    // (Commented out: entity::Human::tick(current_tick);)

    // 3. Auto-save every 6000 ticks (5 minutes at 20 TPS).
    if (spawned_.load(std::memory_order_relaxed) && srv_ &&
        current_tick - last_save_tick_ >= 6000) {
        last_save_tick_ = current_tick;
        if (named_tag_) {
            // Update position in NBT before saving.
            mcp_compound_tag_set_double(named_tag_.get(), "PosX", pos_.x);
            mcp_compound_tag_set_double(named_tag_.get(), "PosY", pos_.y);
            mcp_compound_tag_set_double(named_tag_.get(), "PosZ", pos_.z);
            mcp_compound_tag_set_short(named_tag_.get(), "Health", (int16_t)health());
            mcp_compound_tag_set_short(named_tag_.get(), "Hunger", (int16_t)foodLevel());
            mcp_compound_tag_set_int(named_tag_.get(), "playerGameType", cfg_.gamemode);
            mcp_compound_tag_set_long(named_tag_.get(), "lastPlayed",
                (int64_t)mcp_utils_time_ms());
            srv_->saveOfflinePlayerData(cfg_.username, named_tag_);
        }
    }
}

} // namespace cenisyspe
