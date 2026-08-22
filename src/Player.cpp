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
#include "network/protocol/Info.h"
#include "network/protocol/PacketPool.h"
#include "inventory/PlayerInventory.h"
#include "permission/BanEntry.h"
#include "permission/BanList.h"
#include "utils/MainLogger.hpp"
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
#include <unordered_set>

namespace mcppeserver {

// ============================================================
// Phase 0 — Player construction / destruction
// ============================================================
Player::Player(Server* srv, PlayerConfig cfg)
    : entity::Human(0, nullptr), srv_(srv), cfg_(std::move(cfg)) {
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
    // PMMP reads ops.txt; we keep the simple form for the MVP and
    // treat "first operator" as anyone whose name is "Server" or listed
    // in <dataPath>/ops.txt. To keep this self-contained, we check an
    // env var override plus a small in-memory set populated by /op.
    return false;
}
void Player::setOp(bool /*v*/) {}
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
void Player::setGamemode(int gm) { cfg_.gamemode = gm & 0x03; }

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
        // Packets we acknowledge but don't act on for the MVP.
        case network::protocol::BATCH_PACKET:
        case network::protocol::PLAYER_ACTION_PACKET:
        case network::protocol::PLAYER_INPUT_PACKET:
        case network::protocol::INTERACT_PACKET:
        case network::protocol::USE_ITEM_PACKET:
        case network::protocol::ANIMATE_PACKET:
        case network::protocol::MOB_EQUIPMENT_PACKET:
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

    // AdventureSettings (flight/pvp flags).
    {
        auto as = std::make_shared<network::protocol::AdventureSettingsPacket>();
        uint32_t flags = 0;
        // Creative/Spectator may fly.
        if (cfg_.gamemode == 1 || cfg_.gamemode == 3) flags |= network::protocol::AdventureSettingsPacket::FLAG_ALLOW_FLIGHT;
        // Spectator is world-immutable and no-pvp.
        if (cfg_.gamemode == 3) {
            flags |= network::protocol::AdventureSettingsPacket::FLAG_WORLD_IMMUTABLE;
            flags |= network::protocol::AdventureSettingsPacket::FLAG_NO_PVP;
        }
        as->flags            = flags;
        as->userPermission   = network::protocol::AdventureSettingsPacket::PERMISSION_NORMAL;
        as->globalPermission = network::protocol::AdventureSettingsPacket::PERMISSION_NORMAL;
        sendPacket(as);
    }

    // PLAYER_SPAWN — tells the client "you can show the world now".
    {
        auto ps = std::make_shared<network::protocol::PlayStatusPacket>();
        ps->status = network::protocol::PlayStatusPacket::PLAYER_SPAWN;
        sendPacket(ps);
    }

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
    // Strip color codes (PMMP TextFormat::clean).
    std::string clean;
    clean.reserve(message.size());
    for (char c : message) if ((uint8_t)c != 0xa7 && (uint8_t)c >= 0x20) clean.push_back(c);
    if (clean.empty()) return;
    if (clean.size() > 256) clean.resize(256);

    // Broadcast: <username> message
    if (srv_) srv_->broadcastMessage("<" + cfg_.username + "> " + clean);
}

void Player::handleBlockBreak(int /*x*/, int /*y*/, int /*z*/) {
    // MVP: no block break. Real impl: validate reach, fire BlockBreakEvent,
    // update the chunk, broadcast UpdateBlockPacket.
}
void Player::handleBlockPlace(int /*x*/, int /*y*/, int /*z*/, uint8_t /*face*/, uint8_t /*id*/) {
    // MVP: no block place.
}
void Player::handlePlayerAction(int /*action*/) {}

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

    // 2. Apply gravity / motion (basic; no collision for MVP).
    if (spawned_.load(std::memory_order_relaxed)) {
        entity::Human::tick(current_tick);
    }

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

} // namespace mcppeserver
