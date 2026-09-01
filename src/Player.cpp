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
#include "network/protocol/LevelEventPacket.h"
#include "network/protocol/MobEquipmentPacket.h"
#include "network/protocol/MobArmorEquipmentPacket.h"
#include "network/protocol/AddItemEntityPacket.h"
#include "network/protocol/TakeItemEntityPacket.h"
#include "network/protocol/ContainerSetContentPacket.h"
#include "network/protocol/ContainerSetSlotPacket.h"
#include "network/protocol/Info.h"
#include "network/protocol/PacketPool.h"
#include "inventory/PlayerInventory.h"
#include "inventory/CreativeItems.h"
#include "permission/BanEntry.h"
#include "permission/BanList.h"
#include "command/CommandMap.h"
#include "utils/MainLogger.hpp"
#include "utils/TextColor.h"
#include "utils/Utils.h"
#include "level/format/Chunk.h"
#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/Tag.h"
#include "nbt/tag/ListTag.h"
#include "nbt/tag/ByteTag.h"
#include "nbt/tag/ShortTag.h"
#include "nbt/tag/ByteArrayTag.h"

#include <cmath>
#include <algorithm>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <array>
#include <utility>     // std::pair
#include <random>      // std::mt19937, std::uniform_real_distribution
#include <climits>     // INT32_MIN
#include <mutex>
#include <unordered_set>

namespace atomikpe {

// ============================================================
// Phase 0 — Player construction / destruction
// ============================================================
Player::Player(Server* srv, PlayerConfig cfg)
    : entity::Human(cfg.runtime_eid, nullptr), srv_(srv), cfg_(std::move(cfg)) {
    inv_ = std::make_shared<inventory::PlayerInventory>();
    inv_->setOwner(this);  // so onSlotChange can send packets back to us
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
    // CRITICAL: Do NOT call close() here. close() uses shared_from_this()
    // which throws std::bad_weak_ptr in a destructor (the control block's
    // weak ref is already expired). That exception calls std::terminate()
    // → segfault. This was the root cause of the crash on player disconnect.
    //
    // By the time the destructor runs, close() has already been called
    // (via call_once from the network or main thread) and connected_ is
    // false. If for some reason it wasn't (e.g., Server crash during
    // shutdown), we do a minimal inline save here WITHOUT shared_from_this.
    if (connected_.load(std::memory_order_relaxed)) {
        try {
            if (named_tag_ && srv_ && !cfg_.username.empty()) {
                mcp_compound_tag_set_double(named_tag_.get(), "PosX", pos_.x);
                mcp_compound_tag_set_double(named_tag_.get(), "PosY", pos_.y);
                mcp_compound_tag_set_double(named_tag_.get(), "PosZ", pos_.z);
                mcp_compound_tag_set_float (named_tag_.get(), "Yaw",   yaw_);
                mcp_compound_tag_set_float (named_tag_.get(), "Pitch", pitch_);
                mcp_compound_tag_set_int   (named_tag_.get(), "playerGameType", cfg_.gamemode);
                srv_->saveOfflinePlayerData(cfg_.username, named_tag_);
            }
        } catch (...) {}
        connected_.store(false, std::memory_order_relaxed);
    }
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
// Phase 9 — close (clean shutdown; idempotent + thread-safe)
// ============================================================
// close() may be called from:
//   - The main server thread (player quit via /quit, server shutdown)
//   - The RakLib network thread (session timeout, client disconnect)
//
// CRITICAL: close() must NOT call adv_iface_->closeSession(). When
// close() is called from Network::onSessionClose() (the RakLib
// timeout path), the RakNet session is ALREADY being torn down.
// Calling closeSession() again from within that callback causes a
// re-entrant call into RakLib's C code while its internal state is
// mid-teardown → segfault. This was the root cause of the 100%
// reproducible crash when a player disconnected.
//
// For proactive kicks, Player::kick() calls closeSession() itself
// BEFORE close(), so the RakLib layer handles the disconnect.
// ============================================================
void Player::close(const std::string& reason) {
    // Keep this Player alive for the entire duration of close(), even
    // if Server::tick() concurrently erases its shared_ptr from players_.
    // Without this, the cleanup body below could touch freed memory.
    // We wrap in try-catch because shared_from_this() can throw
    // std::bad_weak_ptr if the control block is already expired (rare
    // edge case during server shutdown). If it throws, we proceed
    // without the safety net — close() is still protected by call_once.
    std::shared_ptr<Player> self;
    try {
        self = shared_from_this();
    } catch (...) {
        // shared_from_this() failed — proceed without the safety net.
    }

    std::call_once(close_once_, [this, &reason]() {
        bool expected = true;
        if (!connected_.compare_exchange_strong(expected, false)) {
            // Already closed — just flag for removal (defensive).
            pending_removal_.store(true, std::memory_order_release);
            return;
        }

        utils::MainLogger::get().info("Closing player " + cfg_.username +
            " (" + cfg_.ip + ":" + std::to_string(cfg_.port) + "): " + reason);

        // 1. Save NBT + inventory to disk (if we ever loaded NBT).
        try {
            if (named_tag_ && srv_ && !cfg_.username.empty()) {
                saveData();
            }
        } catch (...) {
            utils::MainLogger::get().warn("[CLOSE] " + cfg_.username + " saveData threw exception");
        }

        // 2. Tell other players to despawn this entity.
        // CRITICAL: iterate a COPY of the snapshot. The snapshot returns
        // shared_ptrs by value, which keeps each Player alive for the
        // duration of sendPacket. Without this, a concurrent reap in
        // Server::tick() could destroy a Player mid-sendPacket.
        try {
            if (srv_) {
                auto snapshot = srv_->playersSnapshot();
                auto pk = std::make_shared<network::protocol::RemoveEntityPacket>();
                pk->eid = runtimeId();
                for (auto& other : snapshot) {
                    if (!other) continue;
                    if (other.get() == this) continue;
                    if (other->isPendingRemoval()) continue;
                    try {
                        other->sendPacket(pk);
                    } catch (...) {}
                }
            }
        } catch (...) {}

        // 3. Broadcast quit message.
        try {
            if (srv_ && !cfg_.username.empty()) {
                srv_->broadcastMessage(cfg_.username + " left the game");
            }
        } catch (...) {}

        // 4. Release chunks loaded for this player.
        {
            std::lock_guard<std::mutex> lk(chunks_mtx_);
            used_chunks_.clear();
        }
        {
            std::lock_guard<std::mutex> lk(queue_mtx_);
            chunk_queue_.clear();
        }

        // 5. DO NOT call closeSession() here — see the comment at the
        //    top of this function. The session is either already closed
        //    (timeout path) or will be closed by the caller (kick path).

        // 6. Flag for removal — Server::tick() will erase this Player
        //    from players_ on the next tick (main thread).
        pending_removal_.store(true, std::memory_order_release);
    });
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
    // Force-close the RakLib session so the client disconnects promptly.
    // This must happen BEFORE close() because close() no longer calls
    // closeSession() (to avoid re-entrant crashes when called from the
    // RakLib timeout path). closeSession() will trigger RakLib to fire
    // onCloseSession() → close(), but call_once() makes that a no-op.
    if (adv_iface_ && !cfg_.ip.empty()) {
        adv_iface_->closeSession(cfg_.ip, cfg_.port, reason);
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
    // Re-send creative inventory when creative state changes (PMMP
    // Player.php:1461 sends it on every gamemode change).
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
                // Pass the parsed held item id (with held count + damage) so
                // handleBlockPlace can decide: empty hand → no place; block id
                // → place that block, decrement stack (survival only).
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
        case network::protocol::MOB_ARMOR_EQUIPMENT_PACKET:
            // PMMP ignores this server-side; armor changes come via
            // ContainerSetSlotPacket with windowid=0x78 instead.
            break;
        case network::protocol::CONTAINER_SET_SLOT_PACKET:
            // Client moved an item between slots OR picked an item from
            // the creative grid. Mirrors Nukkit Player.onUpdate() handling
            // of ContainerSetSlotPacket:
            //   - windowid 0x00 (SPECIAL_INVENTORY): main inventory slot.
            //     If the player is creative and the item is a valid creative
            //     item, Nukkit calls setItem(slot, item) AND
            //     setHotbarSlotIndex(slot, slot) to link the hotbar.
            //   - windowid 0x78 (SPECIAL_ARMOR): armor slot 0..3.
            //   - windowid 0x7a (SPECIAL_HOTBAR): hotbar slot link.
            //   - windowid 0x79 (SPECIAL_CREATIVE): rarely sent by MCPE
            //     0.15.x clients; handled as a fallback that places the
            //     item into the held hotbar slot.
            if (auto* cs = dynamic_cast<network::protocol::ContainerSetSlotPacket*>(&p)) {
                if (inv_) {
                    atomikpe::item::ItemStack it;
                    it.id    = static_cast<uint16_t>(cs->itemId);
                    it.count = cs->itemCount;
                    it.meta  = static_cast<uint8_t>(cs->itemDamage & 0xFF);

                    // MCPE 0.15.x sometimes sends count=0 for creative items
                    // on the first tap. Nukkit's getSlot() defaults count to
                    // 1 when the id is valid. We do the same so creative
                    // items always have at least 1 in the stack.
                    if (it.id != 0 && it.count == 0) {
                        it.count = 1;
                    }

                    // Debug log for creative inventory clicks.
                    if (srv_ && srv_->advancedLog()) {
                        utils::MainLogger::get().info("[INV] " + cfg_.username +
                            " ContainerSetSlot: wid=0x" +
                            ([](uint8_t v){char b[4];snprintf(b,4,"%02x",v);return std::string(b);})(cs->windowid) +
                            " slot=" + std::to_string(cs->slot) +
                            " hotbar=" + std::to_string(cs->hotbarSlot) +
                            " id=" + std::to_string(it.id) +
                            " count=" + std::to_string((int)it.count));
                    }

                    if (cs->windowid == network::protocol::ContainerSetSlotPacket::SPECIAL_INVENTORY) {
                        // windowid 0x00 — main inventory slots 0..44.
                        // Nukkit: if creative and item is valid, setItem +
                        // setHotbarSlotIndex(slot, slot).
                        if (cs->slot >= 0 && cs->slot < inventory::PlayerInventory::SIZE) {
                            inv_->setItem(cs->slot, it);
                            // If the player is creative and the slot is in
                            // the hotbar range (0..8), link it as the held
                            // slot so the client shows the item in hand.
                            if ((cfg_.gamemode & 1) != 0 && it.id != 0 &&
                                cs->slot >= 0 && cs->slot < 9) {
                                inv_->setHeldSlot(cs->slot);
                            }
                            // CRITICAL: send a ContainerSetSlotPacket back
                            // to confirm the change. Without this, MCPE
                            // 0.15.x reverts the slot visually. Mirrors
                            // Nukkit's onSlotChange → sendSlot().
                            sendSlot(cs->slot);
                        }
                    } else if (cs->windowid == network::protocol::ContainerSetSlotPacket::SPECIAL_ARMOR) {
                        // windowid 0x78 — armor slot 0..3 → store at 36..39.
                        if (cs->slot >= 0 && cs->slot < 4) {
                            int dest = inventory::PlayerInventory::ARMOR_OFFSET + cs->slot;
                            inv_->setItem(dest, it);
                            sendSlot(dest);
                        }
                    } else if (cs->windowid == network::protocol::ContainerSetSlotPacket::SPECIAL_HOTBAR) {
                        // windowid 0x7a — hotbar slot 0..8. This is sent
                        // when the client moves an item to a hotbar slot
                        // directly. Write into main inventory slot 0..8.
                        if (cs->slot >= 0 && cs->slot < 9) {
                            inv_->setItem(cs->slot, it);
                            sendSlot(cs->slot);
                        }
                    } else if (cs->windowid == network::protocol::ContainerSetSlotPacket::SPECIAL_CREATIVE) {
                        // windowid 0x79 — creative-grid click (rare in
                        // MCPE 0.15.x; most clients use windowid=0x00).
                        // Fallback: place the item into the held hotbar slot.
                        if ((cfg_.gamemode & 1) != 0 && it.id != 0) {
                            int held = inv_->heldSlot();
                            if (held >= 0 && held < 9) {
                                inv_->setItem(held, it);
                                sendSlot(held);
                            }
                        }
                    }
                }
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
    if (srv_ && srv_->playersSnapshot().size() >= (size_t)srv_->maxPlayers()) {
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
        for (auto& other : srv_->playersSnapshot()) {
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

            // Last known position — prefer PosX/PosY/PosZ (saved on every
            // auto-save and on disconnect). Falls back to SpawnX/Y/Z (bed
            // spawn) if PosX is missing (first-time players have no Pos).
            // We use a sentinel value to detect "missing" because double
            // getters don't have a natural "missing" value.
            const double sentinel = -99999.999;
            double px = mcp_compound_tag_get_double(named_tag_.get(), "PosX", sentinel);
            if (px != sentinel) {
                pos_.x = px;
                pos_.y = mcp_compound_tag_get_double(named_tag_.get(), "PosY", 64.0);
                pos_.z = mcp_compound_tag_get_double(named_tag_.get(), "PosZ", 0.0);
                yaw_   = mcp_compound_tag_get_float (named_tag_.get(), "Yaw",   0.0f);
                pitch_ = mcp_compound_tag_get_float (named_tag_.get(), "Pitch", 0.0f);
            } else {
                int32_t sx = mcp_compound_tag_get_int(named_tag_.get(), "SpawnX", INT32_MIN);
                int32_t sy = mcp_compound_tag_get_int(named_tag_.get(), "SpawnY", INT32_MIN);
                int32_t sz = mcp_compound_tag_get_int(named_tag_.get(), "SpawnZ", INT32_MIN);
                if (sx != INT32_MIN || sy != INT32_MIN || sz != INT32_MIN) {
                    pos_.x = (double)sx + 0.5; pos_.y = (double)sy; pos_.z = (double)sz + 0.5;
                }
                // else: leave the world-spawn position set in the ctor.
            }

            // --- Load inventory + armor from NBT ---
            // Mirrors cn.nukkit.Player.loadNBT(): iterate the "Inventory"
            // ListTag and route each entry by Slot number. Main inventory
            // uses slots 0..35 (hotbar is 0..8), armor uses 100..103.
            const mcp_tag_t* inv_tag =
                mcp_compound_tag_get(named_tag_.get(), "Inventory");
            if (inv_tag && mcp_tag_get_type(inv_tag) == MCP_TAG_LIST) {
                const mcp_list_tag_t* inv_list =
                    (const mcp_list_tag_t*)inv_tag;
                int loaded = 0;
                for (size_t i = 0; i < inv_list->count; ++i) {
                    const mcp_tag_t* entry = mcp_list_tag_get(inv_tag, i);
                    if (!entry ||
                        mcp_tag_get_type(entry) != MCP_TAG_COMPOUND) continue;
                    int8_t  nbt_slot = mcp_compound_tag_get_byte(
                        entry, "Slot", -1);
                    int16_t id       = mcp_compound_tag_get_short(
                        entry, "id", 0);
                    int8_t  count    = mcp_compound_tag_get_byte(
                        entry, "Count", 1);
                    int16_t damage   = mcp_compound_tag_get_short(
                        entry, "Damage", 0);
                    if (id == 0 || count <= 0) continue;

                    int dest_slot = -1;
                    if (nbt_slot >= 0 && nbt_slot < 36) {
                        // Main inventory + hotbar (slots 0..35).
                        dest_slot = nbt_slot;
                    } else if (nbt_slot >= 100 && nbt_slot <= 103) {
                        // Armor: 100=helmet, 101=chest, 102=legs, 103=boots.
                        dest_slot = inventory::PlayerInventory::ARMOR_OFFSET
                                  + (nbt_slot - 100);
                    } else if (nbt_slot >= 36 && nbt_slot < 45) {
                        // Legacy AtomikPE layout (armor at 36..39).
                        dest_slot = nbt_slot;
                    }
                    if (dest_slot >= 0 &&
                        dest_slot < inventory::PlayerInventory::SIZE) {
                        item::ItemStack it;
                        it.id    = (uint16_t)id;
                        it.meta  = (uint8_t)damage;
                        it.count = count;
                        inv_->setItem(dest_slot, it);
                        ++loaded;
                    }
                }
                utils::MainLogger::get().info("Loaded inventory for " +
                    cfg_.username + " (" + std::to_string(loaded) +
                    " items)");
            }

            // Restore held hotbar slot.
            int32_t held = mcp_compound_tag_get_int(
                named_tag_.get(), "SelectedItemSlot", -1);
            if (held >= 0 && held <= 8) {
                inv_->setHeldSlot(held);
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

    // Send the creative-inventory contents so the player has items to grab
    // in creative mode (PMMP Player.php:2301 sends this right after spawn).
    sendCreativeInventory();

    // Broadcast a PlayerListPacket (ADD) with all currently-online players.
    if (srv_) {
        auto plp = std::make_shared<network::protocol::PlayerListPacket>();
        plp->type = network::protocol::PlayerListPacket::TYPE_ADD;
        for (auto& other : srv_->playersSnapshot()) {
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
        for (auto& other : srv_->playersSnapshot()) {
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
        for (auto& other : srv_->playersSnapshot()) {
            if (other.get() == this) continue;
            other->sendPacket(me);
        }
    }
}

void Player::handleChat(const std::string& message) {
    if (message.empty()) return;

    // PMMP behavior:
    //   - Strip §-codes only for command dispatch (so "/§aversion" still
    //     matches "/version"). Use TC::clean which recognizes the UTF-8 §
    //     sequence (0xC2 0xA7) — the previous inline loop looked for the
    //     bare byte 0xa7, which corrupted the leading 0xC2.
    //   - For plain chat, KEEP the original message (with §-codes intact)
    //     so colors are rendered on every receiver's screen.
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

    // Broadcast UpdateBlockPacket.
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
    // Data:     blockId | (blockMeta << 12)  — note the << 12 shift.
    auto partEv = std::make_shared<network::protocol::LevelEventPacket>();
    partEv->evid = network::protocol::LevelEventPacket::EVENT_PARTICLE_DESTROY;
    partEv->x = (float)x + 0.5f;
    partEv->y = (float)y + 0.5f;
    partEv->z = (float)z + 0.5f;
    partEv->data = ((int32_t)blockId & 0xFF) | ((int32_t)blockData << 12);

    // Broadcast only to players tracking this chunk (PMMP `addChunkPacket`).
    int32_t pk_cx = x >> 4;
    int32_t pk_cz = z >> 4;
    if (srv_) {
        for (auto& other : srv_->playersSnapshot()) {
            if (!other) continue;
            if (!other->isTrackingChunk(pk_cx, pk_cz)) continue;
            other->sendPacket(ubp);
            other->sendPacket(partEv);
        }
    }

    // ---- Drops (PMMP `Level::useBreakOn` -> `Level::dropItem`) ----
    // PMMP: only survival players get drops. Creative players skip.
    bool creative = (cfg_.gamemode & 1) != 0;
    if (!creative) {
        auto drop = lookupBlockDrop(blockId, blockData);
        if (drop.first != 0) {
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

    // ---- Decide what block (if any) to place based on held item ----
    // PMMP-style item→block mapping. Item ids 256+ are tools / buckets /
    // etc. and need special handling. Item ids 1..255 are direct block
    // placements.
    uint8_t placedBlockId   = 0;     // 0 = no placement
    uint8_t placedBlockData = 0;
    bool    consumeItem     = false; // decrement inventory stack?
    bool    skipPlaceSound  = false; // for fire/water/lava (PMMP doesn't play it)

    if (heldItemId == 0) {
        // Empty hand — no placement.
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[BLOCK] " + cfg_.username +
                " attempted place with empty hand — ignored");
        }
        return;
    } else if (heldItemId >= 1 && heldItemId <= 255) {
        // Block item — place it directly. data = low 4 bits of heldDamage.
        placedBlockId   = heldItemId;
        placedBlockData = (uint8_t)(heldDamage & 0x0F);
        consumeItem     = !(cfg_.gamemode & 1);  // survival only
    } else if (heldItemId == 325) {
        // Bucket (empty) — no placement.
        return;
    } else if (heldItemId == 326) {
        // Water bucket → place FLOWING_WATER (id=8) so the client renders
        // it as water that spreads. Using WATER (id=9) causes the client
        // to immediately replace it with a "floating lever" placeholder
        // because id=9 is a source-only block that needs adjacent flow.
        placedBlockId  = 8;
        placedBlockData = 0;
        consumeItem     = !(cfg_.gamemode & 1);
        skipPlaceSound  = true;
    } else if (heldItemId == 327) {
        // Lava bucket → place FLOWING_LAVA (id=10).
        placedBlockId  = 10;
        placedBlockData = 0;
        consumeItem     = !(cfg_.gamemode & 1);
        skipPlaceSound  = true;
    } else if (heldItemId == 259) {
        // Flint and steel → place FIRE (id=51). PMMP BlockFire::place().
        // Bug previously: the generic block-id path rejected 259 because
        // it's >= 256, so it was a no-op. The user reported "places dirt"
        // because the placeholder empty cell sometimes defaulted to dirt
        // visually on the client.
        placedBlockId   = 51;
        placedBlockData = 0;
        // Flint and steel takes 1 damage per use (no stack consumption).
        consumeItem    = false;
        skipPlaceSound = true;
    } else {
        // Other item (food, tool, etc.) — PMMP would call onItemUse; we no-op.
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[BLOCK] " + cfg_.username +
                " used non-block item id=" + std::to_string(heldItemId) + " — no placement");
        }
        return;
    }

    // Calculate placed block position based on face.
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

    // Don't place if target is not air (unless it's fire, which can replace
    // any non-solid block — but for simplicity we always require air here).
    uint8_t existing = mcp_chunk_get_block_id(ch, localX, placeY, localZ);
    if (existing != 0) return;

    // Set the block in the chunk.
    mcp_chunk_set_block_full(ch, localX, placeY, localZ, placedBlockId, placedBlockData);
    ch->dirty = true;

    // Decrement inventory (only in survival — PMMP skips in creative).
    if (consumeItem && inv_) {
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

    // Broadcast UpdateBlockPacket.
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = placeX;
    ubp->z = placeZ;
    ubp->y = (uint8_t)placeY;
    ubp->blockId = placedBlockId;
    ubp->blockData = placedBlockData;
    ubp->flags = 0xFF;

    // PMMP sends a place sound via LevelEventPacket with
    // evid = EVENT_SOUND_BLOCK_PLACE (1052).
    //   - Position: BLOCK CORNER (no +0.5).
    //   - Data:     just the placed block id (no meta bits).
    auto sndEv = std::make_shared<network::protocol::LevelEventPacket>();
    sndEv->evid = network::protocol::LevelEventPacket::EVENT_SOUND_BLOCK_PLACE;
    sndEv->x = (float)placeX;
    sndEv->y = (float)placeY;
    sndEv->z = (float)placeZ;
    sndEv->data = (int32_t)placedBlockId;

    // Same chunk-filter as for block break (PMMP `Level::addSound`).
    int32_t pk_cx = placeX >> 4;
    int32_t pk_cz = placeZ >> 4;
    if (srv_) {
        for (auto& other : srv_->playersSnapshot()) {
            if (!other) continue;
            if (!other->isTrackingChunk(pk_cx, pk_cz)) continue;
            other->sendPacket(ubp);
            // Skip the place sound for fire/water/lava (PMMP behaviour).
            if (!skipPlaceSound) other->sendPacket(sndEv);
        }
    }

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[BLOCK] " + cfg_.username + " placed block at " +
            std::to_string(placeX) + "," + std::to_string(placeY) + "," + std::to_string(placeZ) +
            " (id=" + std::to_string(placedBlockId) + " data=" + std::to_string(placedBlockData) +
            " fromItem=" + std::to_string(heldItemId) + ")");
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
// Block drop lookup table — mirrors PMMP `Block::getDrops(Item $item)`
// per-block overrides. Returns {itemId, damage}. {0, 0} = no drop.
// Default (unlisted block): drop itself (id, damage, count=1).
// ============================================================
static std::pair<uint16_t, uint16_t> lookupBlockDrop(uint8_t blockId, uint8_t blockData) {
    switch (blockId) {
        case 1:  return {4,   blockData & 0x07};   // Stone -> Cobblestone
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
// Drop an item entity at a position (PMMP `Level::dropItem`)
// ============================================================
int64_t Player::dropItem(int x, int y, int z, uint16_t itemId, uint8_t count, uint16_t damage) {
    if (itemId == 0 || count == 0) return 0;
    if (!srv_) return 0;

    // Allocate a runtime entity ID (PMMP `Level::$entityIdCounter`).
    int64_t eid = srv_->allocateEid();

    // CRITICAL: Set speed to 0 so the item stays exactly where it drops.
    // If we give it random motion (like PMMP does), the CLIENT applies
    // the motion visually, but the SERVER's DroppedItem position never
    // updates. This causes a desync: the player walks to where the item
    // VISUALLY is, but the server checks against the ORIGINAL position
    // and says "too far away" → the item can't be picked up.
    // Setting speed=0 ensures the visual position matches the server
    // position, so pickup always works.
    float mx = 0.0f;
    float my = 0.0f;
    float mz = 0.0f;

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
    for (auto& other : srv_->playersSnapshot()) {
        if (!other) continue;
        if (other.get() == this) {
            // Always send to the dropper too so they see their own drop.
        } else if (!other->isTrackingChunk(pk_cx, pk_cz)) {
            continue;
        }
        other->sendPacket(pk);
    }

    // Register the dropped item in the Level's registry so other players
    // (and the dropper themselves, after the pickup delay) can pick it up.
    if (srv_->getDefaultLevel()) {
        level::DroppedItem di;
        di.eid        = eid;
        di.itemId     = itemId;
        di.count      = count;
        di.damage     = damage;
        di.x          = (double)x + 0.5;
        di.y          = (double)y + 0.5;
        di.z          = (double)z + 0.5;
        di.spawn_tick = srv_->getCurrentTick();
        // 5-tick pickup delay (~0.25s) — just enough to prevent the
        // dropper from instantly re-absorbing it, but short enough that
        // the player can pick it up quickly after breaking a block.
        di.pickup_delay_ticks = 5;
        di.taken     = false;
        srv_->getDefaultLevel()->addDroppedItem(di);
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
// Send the creative-inventory contents (PMMP Player.php:2301 + 1461)
// ============================================================
void Player::sendCreativeInventory() {
    bool spectator = (cfg_.gamemode == 3);
    auto pk = std::make_shared<network::protocol::ContainerSetContentPacket>();
    pk->windowid = network::protocol::ContainerSetContentPacket::SPECIAL_CREATIVE;
    if (!spectator) {
        // Use the hardcoded creative-items list (no JSON dependency —
        // mirrors Nukkit's approach of compiling the list at build time).
        pk->slots = inventory::getCreativeItems();
    }
    sendPacket(pk);
}

// ============================================================
// sendInventory — push the player's main inventory + armor to the client.
// Called on doFirstSpawn() and after any inventory change so the client
// UI stays in sync with the server's authoritative state. Mirrors
// Nukkit Player.sendInventory() + Player.sendArmor().
// ============================================================
void Player::sendInventory() {
    if (!inv_) return;

    // Main inventory (windowid 0x00): 45 slots (36 main + 9 hotbar).
    {
        auto pk = std::make_shared<network::protocol::ContainerSetContentPacket>();
        pk->windowid = network::protocol::ContainerSetContentPacket::SPECIAL_INVENTORY;
        pk->slots.reserve(inventory::PlayerInventory::SIZE);
        for (int i = 0; i < inventory::PlayerInventory::SIZE; ++i) {
            pk->slots.push_back(inv_->getItem(i));
        }
        // Hotbar link indices — protocol 84 wants 9 indices for the hotbar.
        for (int h = 0; h < 9; ++h) {
            pk->hotbar.push_back(h);
        }
        sendPacket(pk);
    }

    // Armor (windowid 0x78): 4 slots.
    {
        auto pk = std::make_shared<network::protocol::ContainerSetContentPacket>();
        pk->windowid = network::protocol::ContainerSetContentPacket::SPECIAL_ARMOR;
        pk->slots.push_back(inv_->getHelmet());
        pk->slots.push_back(inv_->getChestplate());
        pk->slots.push_back(inv_->getLeggings());
        pk->slots.push_back(inv_->getBoots());
        sendPacket(pk);
    }

    // Hotbar (windowid 0x7a): 9 slots mirroring the hotbar portion of
    // the main inventory (slots 0..8). Nukkit sends this too so the
    // client immediately highlights the correct hotbar slot.
    {
        auto pk = std::make_shared<network::protocol::ContainerSetContentPacket>();
        pk->windowid = network::protocol::ContainerSetContentPacket::SPECIAL_HOTBAR;
        for (int h = 0; h < 9; ++h) {
            pk->slots.push_back(inv_->getItem(h));
        }
        sendPacket(pk);
    }
}

// ============================================================
// sendSlot — send a ContainerSetSlotPacket for a single slot back to
// the client. Mirrors Nukkit's PlayerInventory.sendSlot(index, player):
// after the server processes a ContainerSetSlotPacket from the client
// (e.g. creative inventory click), it MUST send a confirmation packet
// back. Without this, MCPE 0.15.x reverts the slot change visually —
// the item disappears from the client UI even though it's in the
// server's inventory.
// ============================================================
void Player::sendSlot(int slot) {
    if (!inv_) return;

    // Determine the windowid and the actual slot index.
    uint8_t windowid;
    int actual_slot = slot;

    if (slot >= inventory::PlayerInventory::ARMOR_OFFSET &&
        slot <  inventory::PlayerInventory::ARMOR_OFFSET + 4) {
        // Armor slot — windowid 0x78, slot 0..3.
        windowid = network::protocol::ContainerSetSlotPacket::SPECIAL_ARMOR;
        actual_slot = slot - inventory::PlayerInventory::ARMOR_OFFSET;
    } else if (slot >= 0 && slot < inventory::PlayerInventory::SIZE) {
        // Main inventory — windowid 0x00.
        windowid = network::protocol::ContainerSetSlotPacket::SPECIAL_INVENTORY;
    } else {
        return;  // Invalid slot.
    }

    auto pk = std::make_shared<network::protocol::ContainerSetSlotPacket>();
    pk->windowid   = windowid;
    pk->slot       = static_cast<int16_t>(actual_slot);
    pk->hotbarSlot = static_cast<int16_t>(actual_slot);
    auto item = inv_->getItem(slot);
    pk->itemId     = static_cast<int16_t>(item.id);
    pk->itemCount  = item.count;
    pk->itemDamage = static_cast<int16_t>(item.meta);
    sendPacket(pk);
}

// ============================================================
// Save this player's NBT + inventory to disk (PMMP Player::saveNBT)
// ============================================================
// Mirrors cn.nukkit.Player.saveNBT(): the NBT root contains a flat
// "Inventory" ListTag<CompoundTag> with one entry per non-empty slot.
// Each entry is { Slot:Byte, id:Short, Count:Byte, Damage:Short }.
// Armor uses the same list with Slot in [100..103] (helmet..boots).
// On load we iterate the list and route by Slot range — mirroring
// Nukkit's Player.loadNBT() exactly so worlds are interchangeable.
// ============================================================
void Player::saveData() {
    if (!srv_ || cfg_.username.empty()) return;
    if (!named_tag_) {
        // Construct a fresh NBT root if none exists.
        named_tag_ = std::shared_ptr<::mcp_tag>(
            mcp_compound_tag_new(),
            [](::mcp_tag* t) { if (t) mcp_tag_destroy(t); });
    }
    ::mcp_tag* root = named_tag_.get();
    if (!root) return;

    // --- Position / rotation / stats ---
    mcp_compound_tag_set_double(root, "PosX",  pos_.x);
    mcp_compound_tag_set_double(root, "PosY",  pos_.y);
    mcp_compound_tag_set_double(root, "PosZ",  pos_.z);
    mcp_compound_tag_set_float (root, "Yaw",    yaw_);
    mcp_compound_tag_set_float (root, "Pitch",  pitch_);
    mcp_compound_tag_set_int   (root, "playerGameType", cfg_.gamemode);
    mcp_compound_tag_set_short (root, "Health",  (int16_t)health());
    mcp_compound_tag_set_short (root, "MaxHealth", (int16_t)maxHealth());
    mcp_compound_tag_set_short (root, "Hunger",  (int16_t)foodLevel());
    mcp_compound_tag_set_short (root, "Air",     300);
    mcp_compound_tag_set_long  (root, "RuntimeId", runtimeId());
    mcp_compound_tag_set_long  (root, "lastPlayed",
        (int64_t)mcp_utils_time_ms());

    // --- Inventory + Armor as a single ListTag<CompoundTag> ---
    mcp_tag_t* inv_list = mcp_list_tag_new_named("Inventory", MCP_TAG_COMPOUND);
    if (inv_list && inv_) {
        for (int slot = 0; slot < inventory::PlayerInventory::SIZE; ++slot) {
            auto it = inv_->getItem(slot);
            if (it.isEmpty()) continue;
            int nbt_slot = slot;
            if (slot >= inventory::PlayerInventory::ARMOR_OFFSET &&
                slot <  inventory::PlayerInventory::ARMOR_OFFSET + 4) {
                nbt_slot = 100 + (slot - inventory::PlayerInventory::ARMOR_OFFSET);
            }
            mcp_tag_t* entry = mcp_compound_tag_new_named("");
            mcp_compound_tag_set_byte (entry, "Slot",   (int8_t)nbt_slot);
            mcp_compound_tag_set_short(entry, "id",     (int16_t)it.id);
            mcp_compound_tag_set_byte (entry, "Count",  (int8_t)it.count);
            mcp_compound_tag_set_short(entry, "Damage", (int16_t)it.meta);
            mcp_list_tag_append(inv_list, entry);
        }
        mcp_compound_tag_set_int(root, "SelectedItemSlot",
            inv_->heldSlot());
    }
    mcp_compound_tag_set(root, "Inventory", inv_list);

    srv_->saveOfflinePlayerData(cfg_.username, named_tag_);
    if (srv_->advancedLog()) {
        utils::MainLogger::get().info("[SAVE] Player " + cfg_.username + " data saved.");
    }
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
            // C++ generates chunks in ~0.22ms each (benchmark proven).
            // At 4 chunks/tick that's ~0.88ms — well under the 50ms
            // tick budget. This allows fast world loading without
            // blocking the network tick.
            constexpr int MAX_CHUNKS_PER_TICK = 4;
            int chunks_sent_this_tick = 0;
            for (auto& pr : order) {
                int32_t chX = cx + pr.first, chZ = cz + pr.second;
                int64_t hash = ((int64_t)chX << 32) | ((uint32_t)chZ);
                still_in_view.insert(hash);
                bool already_sent;
                { std::lock_guard<std::mutex> lk(chunks_mtx_); already_sent = used_chunks_.count(hash) > 0; }
                if (already_sent) continue;
                if (chunks_sent_this_tick >= MAX_CHUNKS_PER_TICK) continue;
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
                chunks_sent_this_tick++;
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

    // 2.5. Pickup nearby dropped items. Mirrors Nukkit Player.checkNearEntities():
    // for each EntityItem within the player's bounding box grown by (1, 0.5, 1),
    // if pickupDelay has elapsed AND the inventory can hold the item, send
    // TakeItemEntityPacket to all viewers + the picking player, addItem to
    // the inventory, and mark the entity for removal.
    if (spawned_.load(std::memory_order_relaxed) && srv_ && srv_->getDefaultLevel() && inv_) {
        auto* lvl = srv_->getDefaultLevel();
        auto dropped = lvl->droppedItems();
        bool any_picked_up = false;
        for (level::DroppedItem* di : dropped) {
            if (!di || di->taken) continue;
            if (current_tick - di->spawn_tick < di->pickup_delay_ticks) continue;
            double dx = di->x - pos_.x;
            double dy = di->y - pos_.y;
            double dz = di->z - pos_.z;
            double dist_sq = dx*dx + dy*dy + dz*dz;
            if (dist_sq > 6.25) continue;

            atomikpe::item::ItemStack pickup;
            pickup.id    = di->itemId;
            pickup.count = di->count;
            pickup.meta  = (uint8_t)(di->damage & 0xFF);

            bool can_pickup;
            if ((cfg_.gamemode & 1) != 0) {
                can_pickup = true;
            } else {
                can_pickup = inv_->canAddItem(pickup);
            }
            if (!can_pickup) continue;

            // Add to inventory FIRST.
            std::vector<atomikpe::item::ItemStack> leftovers =
                inv_->addItem({pickup});
            int leftover_count = leftovers.empty() ? 0 : leftovers[0].count;
            int absorbed = pickup.count - leftover_count;
            if (absorbed <= 0) continue;

            // FIX #1: Atomic claim — prevents race condition where two
            // players pick up the same item in the same tick.
            // tryClaimDroppedItem sets taken=true under mutex. If another
            // player already claimed it, we rollback the addItem.
            if (!lvl->tryClaimDroppedItem(di->eid)) {
                // Another player got it first — rollback the inventory
                // addition by removing the items we just added.
                atomikpe::item::ItemStack rollback;
                rollback.id = pickup.id;
                rollback.meta = pickup.meta;
                rollback.count = absorbed;
                inv_->removeItem({rollback});
                continue;
            }

            // FIX #6: For partial pickup, do NOT send TakeItemEntityPacket
            // to other viewers — they would see the item disappear and
            // reappear. Only send TakeItemEntityPacket for full pickup.
            if (leftover_count == 0) {
                // Full pickup — send TakeItemEntityPacket to viewers
                // + RemoveEntityPacket to picker.
                auto pk_viewer = std::make_shared<network::protocol::TakeItemEntityPacket>();
                pk_viewer->target   = di->eid;
                pk_viewer->entityId = runtimeId();
                int32_t pk_cx = (int32_t)di->x >> 4;
                int32_t pk_cz = (int32_t)di->z >> 4;
                for (auto& other : srv_->playersSnapshot()) {
                    if (!other) continue;
                    if (other.get() == this) continue;
                    if (other->isPendingRemoval()) continue;
                    if (!other->isTrackingChunk(pk_cx, pk_cz)) continue;
                    other->sendPacket(pk_viewer);
                }
                auto pk_picker = std::make_shared<network::protocol::TakeItemEntityPacket>();
                pk_picker->target   = di->eid;
                pk_picker->entityId = 0;
                sendPacket(pk_picker);
                lvl->removeDroppedItem(di->eid);
                auto rm_pk = std::make_shared<network::protocol::RemoveEntityPacket>();
                rm_pk->eid = di->eid;
                sendPacket(rm_pk);
            } else {
                // Partial pickup — update the dropped item's count.
                // Reset taken=false so it can be picked up again.
                // Do NOT send TakeItemEntityPacket to viewers — only
                // the picker should know (via inventory update).
                di->count = (uint8_t)leftover_count;
                di->taken = false;
            }

            any_picked_up = true;
        }
        if (any_picked_up) {
            sendInventory();
        }
    }

    // 3. Auto-save every 6000 ticks (5 minutes at 20 TPS).
    if (spawned_.load(std::memory_order_relaxed) && srv_ &&
        current_tick - last_save_tick_ >= 6000) {
        last_save_tick_ = current_tick;
        if (named_tag_) {
            // Update position+rotation in NBT before saving (mirrors
            // PMMP Player::saveNBT — must include Yaw/Pitch).
            mcp_compound_tag_set_double(named_tag_.get(), "PosX", pos_.x);
            mcp_compound_tag_set_double(named_tag_.get(), "PosY", pos_.y);
            mcp_compound_tag_set_double(named_tag_.get(), "PosZ", pos_.z);
            mcp_compound_tag_set_float (named_tag_.get(), "Yaw",   yaw_);
            mcp_compound_tag_set_float (named_tag_.get(), "Pitch", pitch_);
            mcp_compound_tag_set_short(named_tag_.get(), "Health", (int16_t)health());
            mcp_compound_tag_set_short(named_tag_.get(), "Hunger", (int16_t)foodLevel());
            mcp_compound_tag_set_int  (named_tag_.get(), "playerGameType", cfg_.gamemode);
            mcp_compound_tag_set_long  (named_tag_.get(), "lastPlayed",
                (int64_t)mcp_utils_time_ms());
            srv_->saveOfflinePlayerData(cfg_.username, named_tag_);
        }
    }
}

// ============================================================
// Stub implementations for Nukkit-compat methods
// ============================================================
void Player::sendPosition(double x, double y, double z, double yaw, double pitch, uint8_t mode) {
    auto pk = std::make_shared<network::protocol::MovePlayerPacket>();
    pk->eid = runtimeId();
    pk->x = (float)x; pk->y = (float)y; pk->z = (float)z;
    pk->yaw = (float)yaw; pk->bodyYaw = (float)yaw; pk->pitch = (float)pitch;
    pk->mode = mode;
    pk->onGround = onGround();
    sendPacket(pk);
}
void Player::sendPopup(const std::string& msg) {
    auto pk = std::make_shared<network::protocol::TextPacket>();
    pk->type = network::protocol::TextPacket::TYPE_POPUP;
    pk->message = msg;
    sendPacket(pk);
}
void Player::sendTip(const std::string& msg) {
    auto pk = std::make_shared<network::protocol::TextPacket>();
    pk->type = network::protocol::TextPacket::TYPE_TIP;
    pk->message = msg;
    sendPacket(pk);
}
void Player::sendTranslation(const std::string& key) {
    sendMessage(key);
}

// --- Nukkit-compat: getSpawn ---
math::Vector3 Player::getSpawn() const {
    if (srv_ && srv_->getDefaultLevel()) return srv_->getDefaultLevel()->spawn();
    return math::Vector3{0, 64, 0};
}
void Player::setExperience(int exp, int level) {
    (void)exp; (void)level;
}
void Player::sendExperience(int exp) { (void)exp; }
void Player::sendExperienceLevel(int level) { (void)level; }
void Player::setHealth(int h) { entity::Living::setHealth((uint16_t)h); }
bool Player::isChunkLoaded(int cx, int cz) const {
    int64_t hash = ((int64_t)cx << 32) | ((uint32_t)cz);
    std::lock_guard<std::mutex> lk(chunks_mtx_);
    return used_chunks_.count(hash) > 0;
}
bool Player::isChunkLoading(int cx, int cz) const {
    return false;
}
void Player::unloadChunk(int cx, int cz) {
    int64_t hash = ((int64_t)cx << 32) | ((uint32_t)cz);
    std::lock_guard<std::mutex> lk(chunks_mtx_);
    used_chunks_.erase(hash);
}
void Player::sendChunk(int cx, int cz) {
    if (!srv_ || !srv_->getDefaultLevel()) return;
    auto& chunks = srv_->getDefaultLevel()->chunks();
    mcp_chunk_t* ch = chunks.getChunk(cx, cz);
    if (!ch) return;
    size_t outLen = 0;
    uint8_t* buf = mcp_chunk_network_serialize(ch, &outLen);
    if (!buf || outLen == 0) return;
    auto pk = std::make_shared<network::protocol::FullChunkDataPacket>();
    pk->chunkX = cx; pk->chunkZ = cz;
    pk->data.assign(buf, buf + outLen);
    free(buf);
    sendPacket(pk);
    int64_t hash = ((int64_t)cx << 32) | ((uint32_t)cz);
    std::lock_guard<std::mutex> lk(chunks_mtx_);
    used_chunks_.insert(hash);
}
void Player::spawnTo(Player* player) {
    if (!player || player == this) return;
    auto ap = std::make_shared<network::protocol::AddPlayerPacket>();
    parseUuidBytes(cfg_.uuid, ap->uuid);
    ap->username = cfg_.username;
    ap->eid = runtimeId();
    ap->x = (float)pos_.x; ap->y = (float)pos_.y; ap->z = (float)pos_.z;
    ap->yaw = yaw_; ap->headYaw = yaw_; ap->pitch = pitch_;
    player->sendPacket(ap);
}
bool Player::canInteract(math::Vector3 pos, double maxDistance) const {
    double dx = pos.x - pos_.x;
    double dy = pos.y - pos_.y;
    double dz = pos.z - pos_.z;
    return (dx*dx + dy*dy + dz*dz) <= maxDistance * maxDistance;
}
std::vector<item::ItemStack> Player::getDrops() const {
    std::vector<item::ItemStack> drops;
    if (inv_) {
        for (int i = 0; i < inventory::PlayerInventory::SIZE; ++i) {
            auto it = inv_->getItem(i);
            if (!it.isEmpty()) drops.push_back(it);
        }
    }
    return drops;
}
void Player::setMovementSpeed(float speed) { (void)speed; }
void Player::setAttribute(int attrId, float value) { (void)attrId; (void)value; }
void Player::sendSettings() {
    auto as = std::make_shared<network::protocol::AdventureSettingsPacket>();
    uint32_t flags = 0;
    flags |= 0x02; flags |= 0x04; flags |= 0x08; flags |= 0x40;
    if ((cfg_.gamemode & 1) != 0) flags |= 0x80;
    as->flags = flags;
    as->userPermission = 2;
    as->globalPermission = 2;
    sendPacket(as);
}
bool Player::batchDataPacket(const std::vector<uint8_t>& data) {
    (void)data;
    return true;
}

} // namespace atomikpe
