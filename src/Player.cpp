#include "Player.h"
#include "player/PlayerInternal.h"
#include "workbench/WorkbenchManager.h"
#include "diagnostics/Diagnostics.h"
#include "Server.h"
#include <exception>
#include <stdexcept>
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
#include "network/protocol/SetEntityDataPacket.h"
#include "network/protocol/SetEntityMotionPacket.h"
#include "network/protocol/LevelEventPacket.h"
#include "network/protocol/AnimatePacket.h"
#include "network/protocol/MobEquipmentPacket.h"
#include "network/protocol/MobArmorEquipmentPacket.h"
#include "network/protocol/AddItemEntityPacket.h"
#include "network/protocol/TakeItemEntityPacket.h"
#include "item/ItemFlintSteel.h"
#include "item/Item.h"
#include "block/BlockFire.h"
#include "redstone/RedstoneManager.h"
#include "item/ItemConstants.h"
#include "block/BlockIds.h"
#include "tile/TileFurnace.h"
#include "inventory/FurnaceInventory.h"
#include "block/BlockOrientation.h"
#include "event/EventManager.h"
#include "event/player/PlayerJoinEvent.h"
#include "event/player/PlayerQuitEvent.h"
#include "event/player/PlayerChatEvent.h"
#include "event/block/BlockBreakEvent.h"
#include "event/block/BlockPlaceEvent.h"
#include "event/player/PlayerMoveEvent.h"
#include "event/player/PlayerInteractEvent.h"
#include "event/player/PlayerTeleportEvent.h"
#include "event/player/PlayerDeathEvent.h"
#include "event/player/PlayerRespawnEvent.h"
#include "event/inventory/InventoryClickEvent.h"
#include "network/protocol/InteractPacket.h"
#include "network/protocol/EntityEventPacket.h"
#include "network/protocol/RespawnPacket.h"
#include "network/protocol/UpdateAttributesPacket.h"
#include "network/protocol/DropItemPacket.h"
#include "network/protocol/ContainerSetContentPacket.h"
#include "network/protocol/ContainerSetSlotPacket.h"
#include "network/protocol/ContainerOpenPacket.h"
#include "network/protocol/ContainerClosePacket.h"
#include "network/protocol/CraftingDataPacket.h"
#include "network/protocol/CraftingEventPacket.h"
#include "network/protocol/ContainerSetDataPacket.h"
#include "network/protocol/BlockEntityDataPacket.h"
#include "network/protocol/ItemFrameDropItemPacket.h"
#include "event/player/PlayerDropItemEvent.h"
#include "event/player/PlayerItemConsumeEvent.h"
#include "event/block/SignChangeEvent.h"
#include "tile/TileSign.h"
#include "nbt/NBT.h"
#include "nbt/tag/CompoundTag.h"
#include "event/inventory/InventoryOpenEvent.h"
#include "event/inventory/InventoryClickEvent.h"
#include "event/inventory/CraftItemEvent.h"
#include "tile/TileChest.h"
#include "block/BlockIds.h"
#include "inventory/CraftingManager.h"
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
#include <cstdlib>
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

namespace cursestone {

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
    // Hunger state — Infinity Player.php: default saturation 5, food 20.
    food_saturation_ = 5.0f;
    food_tick_timer_ = 0;
    food_depletion_ = 0;
    last_pos_set_ = false;
    was_in_lava_ = false;
    was_on_fire_ = false;
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

        // Release any chest this player still had open. connected_ is
        // already false at this point, so closeOpenChest()'s
        // ContainerClosePacket send is a safe no-op (sendPacket() bails
        // out when !connected_) — what actually matters here is
        // decrementing the TileChest's viewer count and dropping our
        // reference to it, so the chest doesn't think a disconnected
        // player is still looking at it.
        closeOpenChest();

        // Workbench: capture disconnect state for diagnostics.
        {
            uint64_t now_ms = (uint64_t)(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            uint64_t session_ms = now_ms - (start_time_ms_ > 0 ? start_time_ms_ : now_ms);
            uint64_t last_act = now_ms - (last_activity_ms_ > 0 ? last_activity_ms_ : now_ms);
            size_t chunk_q = 0;
            { std::lock_guard<std::mutex> lk(queue_mtx_); chunk_q = chunk_queue_.size(); }
            WorkbenchManager::get().onDisconnect(cfg_.username, reason,
                session_ms, last_act, chunk_q);
        }

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
        //
        // BUG FIX (mismo mecanismo que el de MoveEntityPacket en
        // handleMovePlayer/EntityEventPacket en el combate): si `other`
        // todavía está en su propia ventana de streaming de chunks (sin
        // spawned() todavía), nunca llegó a recibir el AddPlayerPacket de
        // esta entidad — mandarle igual el RemoveEntityPacket lo hace
        // referenciar un eid que su cliente nunca conoció.
        try {
            if (srv_) {
                auto snapshot = srv_->playersSnapshot();
                auto pk = std::make_shared<network::protocol::RemoveEntityPacket>();
                pk->eid = runtimeId();
                for (auto& other : snapshot) {
                    if (!other) continue;
                    if (other.get() == this) continue;
                    if (other->isPendingRemoval()) continue;
                    if (!other->spawned()) continue;
                    try {
                        other->sendPacket(pk);
                    } catch (...) {}
                }
            }
        } catch (...) {}

        // 3. Broadcast quit message. PlayerQuitEvent puede reescribir o
        // silenciar el mensaje. Envuelto en try/catch como el resto de
        // close(): un listener con un bug jamás debe impedir que la
        // limpieza de la desconexión continúe.
        try {
            if (srv_ && !cfg_.username.empty()) {
                auto ev = event::EventManager::get().fire<event::player::PlayerQuitEvent>(
                    this, cfg_.username + " left the game");
                if (!ev.isCancelled() && !ev.message.empty()) {
                    srv_->broadcastMessage(ev.message);
                }
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

// Ver la nota larga en Player.h. Criterio de DISTANCIA, no de bookkeeping:
// "¿esta el chunk dentro de mi distancia de vision?".
bool Player::canSeeBlock(int32_t cx, int32_t cz) const {
    int32_t pcx = (int32_t)std::floor(pos_.x) >> 4;
    int32_t pcz = (int32_t)std::floor(pos_.z) >> 4;
    int32_t vd  = cfg_.view_distance;
    if (vd < 1) vd = 1;
    int32_t dx = cx - pcx, dz = cz - pcz;
    if (dx < 0) dx = -dx;
    if (dz < 0) dz = -dz;
    return dx <= vd && dz <= vd;
}

void Player::kick(const std::string& reason) {
    kick(reason, "");
}

void Player::kick(const std::string& reason, const std::string& detail) {
    // Send a DisconnectPacket first, then close the RakNet session.
    //
    // Nukkit parity: the disconnect screen the client renders carries the
    // full message, so server-side errors that lead to a kick are shown to
    // the player instead of vanishing into the console. `detail` is the
    // extra diagnostic line (e.g. the exception message).
    std::string message = reason;
    if (!detail.empty()) {
        message += "\n";
        message += detail;
    }
    if (connected_.load(std::memory_order_relaxed)) {
        auto dp = std::make_shared<network::protocol::DisconnectPacket>();
        dp->message = message;
        sendPacket(dp);
    }
    // Force-close the RakLib session so the client disconnects promptly.
    // This must happen BEFORE close() because close() no longer calls
    // closeSession() (to avoid re-entrant crashes when called from the
    // RakLib timeout path). closeSession() will trigger RakLib to fire
    // onCloseSession() → close(), but call_once() makes that a no-op.
    if (adv_iface_ && !cfg_.ip.empty()) {
        adv_iface_->closeSession(cfg_.ip, cfg_.port, message);
    }
    close(reason);
}

void Player::teleport(double x, double y, double z) {
    // PlayerTeleportEvent — un listener puede cancelarlo (p.ej. un
    // comando /tp bloqueado por permisos, o una zona que prohibe
    // teletransportarse hacia adentro) para impedir el movimiento sin
    // que el llamador tenga que chequear un valor de retorno: como
    // teleport() no devolvia nada antes de este cambio, ningun sitio
    // del codigo depende de que "siempre se mueva".
    auto ev = event::EventManager::get().fire<event::player::PlayerTeleportEvent>(
        this, pos_.x, pos_.y, pos_.z, x, y, z);
    if (ev.isCancelled()) return;

    int32_t old_cx = (int)pos_.x >> 4;
    int32_t old_cz = (int)pos_.z >> 4;
    pos_.x = ev.toX; pos_.y = ev.toY; pos_.z = ev.toZ;
    int32_t new_cx = (int)pos_.x >> 4;
    int32_t new_cz = (int)pos_.z >> 4;

    // CHUNK FIX ("chunks missing after /tp or respawn"): the client
    // renders nothing around the destination until we send it there, and
    // the streaming loop in onUpdate() only reacts to CROSSING a chunk
    // boundary via its `moved` detection. That detection compares against
    // chunk_queue_'s front entry — after a teleport the queue still held
    // the OLD center, so the very next tick recorded "moved" once, then
    // recentered; but every chunk in the new radius that had already been
    // marked in used_chunks_ by an earlier pass through that area (or by
    // the spawn-ACK re-stream) was SKIPPED as "already sent" even though
    // the client teleported away and may no longer have them loaded.
    // Clearing the per-player sent-set + re-queuing the new spiral makes
    // the destination stream exactly like a fresh spawn: chunks the
    // client still holds arrive as duplicates it ignores, missing ones
    // now render. Only do this when the teleport actually crossed into
    // another chunk — a 2-block hop inside the same chunk needs nothing.
    if (new_cx != old_cx || new_cz != old_cz) {
        {
            std::lock_guard<std::mutex> lk(chunks_mtx_);
            used_chunks_.clear();
        }
        {
            std::lock_guard<std::mutex> lk(queue_mtx_);
            chunk_queue_.clear();
            chunk_queue_.emplace_back(new_cx, new_cz);
        }
    }

    // Optionally broadcast a MovePlayerPacket to confirm position to the client.
    if (spawned_.load(std::memory_order_relaxed)) {
        auto pk = std::make_shared<network::protocol::MovePlayerPacket>();
        pk->eid = runtimeId();
        pk->x   = (float)pos_.x;
        pk->y   = (float)(pos_.y + kEyeHeight);   // wire Y is the eye
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
    // BUG FIX ("items disappear when switching gamemode"): the block
    // above only refreshes the read-only creative CATALOG (windowid
    // SPECIAL_CREATIVE) — it never touches the player's actual
    // inventory (main/hotbar/armor). Every real item move (dragging
    // from the creative catalog, moving items around) is confirmed
    // one slot at a time via ContainerSetSlotPacket on the same
    // unordered channel that SetPlayerGameTypePacket/
    // AdventureSettingsPacket go out on right after this call (see
    // GamemodeCommand.cpp). If any of those land out of order or the
    // client's UI rebuild (triggered by the mode-switch packet) races
    // ahead of the last slot confirmation, the client is left
    // displaying a stale picture — some slots got their confirmation,
    // others didn't, so a handful of items appear to "vanish" while
    // the rest survive. Forcing a full, authoritative resync of main
    // inventory + armor + hotbar right here — same as what already
    // happens once at doFirstSpawn() — guarantees the client always
    // ends a mode switch showing exactly what the server has, instead
    // of whatever partial state survived the race.
    // Guarded on spawned_: setGamemode() also runs during handleLogin()
    // (loading playerGameType from NBT) BEFORE StartGamePacket/
    // PlayStatus have even been sent — sending inventory packets that
    // early, ahead of the login handshake, would confuse the client
    // the same way the ordered=true fixes elsewhere in this file exist
    // to prevent.
    if (spawned_.load(std::memory_order_relaxed)) {
        sendInventory();
    }
}

// Helper: "0x%02X" de un byte, para los logs de diagnóstico.
static std::string hex8(uint8_t v) {
    static const char* d = "0123456789ABCDEF";
    std::string s = "0x";
    s += d[v >> 4];
    s += d[v & 0x0F];
    return s;
}

// Helper: transmite un cambio de bloque puntual a todos los jugadores
// que estén viendo ese chunk. Se usa para componentes de redstone que
// cambian más de un bloque (por ejemplo, puertas).
static void broadcastBlockChange(cursestone::Server* server, int32_t x, int32_t y, int32_t z,
                                   uint8_t blockId, uint8_t blockData) {
    if (!server) return;
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = x;
    ubp->z = z;
    ubp->y = (uint8_t)y;
    ubp->blockId = blockId;
    ubp->blockData = blockData;
    ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
    int32_t cx = x >> 4, cz = z >> 4;
    for (auto& player : server->playersSnapshot()) {
        if (!player) continue;
        if (!player->canSeeBlock(cx, cz)) continue;
        player->sendPacket(ubp);
    }
}

// Difunde la pose (agachado + sprint) a todos los clientes.
//
// Genisys 0.15.0 no hace esto: Player::setSneaking() (Player.php:3017)
// solo cambia una propiedad local del Player en el servidor y nunca
// emite SetEntityDataPacket, asi que en un server Genisys el agachado
// tampoco seria visible para los demas. El formato del paquete si
// viene de ahi (SetEntityDataPacket + Binary::writeMetadata), asi que
// lo que falta es la llamada.
//
// La pose va en Entity::DATA_FLAGS = 0 (byte), donde el bit 1 es
// SNEAKING y el bit 3 es SPRINTING (Genisys Entity.php:87-105). Como
// los dos bits viven en la misma propiedad hay que mandar SIEMPRE el
// byte completo; mandar solo un bit pisaria el otro en el cliente que
// lo recibe.
//
// Genisys Entity::sendData() usa eid = 0 cuando el destino es la propia
// entidad (sendData($this)); para el resto, el runtime id real.
void Player::broadcastPoseFlags() {
    if (!srv_) return;
    auto pk = std::make_shared<network::protocol::SetEntityDataPacket>();
    pk->eid = runtimeId();
    pk->entries.push_back(makePoseFlagEntry(sneaking_, sprinting_));

    // Nos lo mandamos a nosotros con eid = 0, igual que Genisys, para
    // que el cliente own no se quede con una pose contradicha.
    {
        auto self = std::make_shared<network::protocol::SetEntityDataPacket>();
        self->eid = 0;
        self->entries.push_back(makePoseFlagEntry(sneaking_, sprinting_));
        sendPacket(self);
    }

    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this) continue;
        if (other->isPendingRemoval()) continue;
        if (!other->spawned()) continue;
        other->sendPacket(pk);
    }
}

void Player::setSneaking(bool v) {
    if (sneaking_ == v) return;
    sneaking_ = v;
    broadcastPoseFlags();
}

// Helper: parse a "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" UUID string into
// 16 raw bytes. Empty input yields zeros. Used by AddPlayerPacket / PlayerListPacket.
// (Moved above doFirstSpawn; see earlier definition.)

// ============================================================
// Phase 7 — handleMovePlayer (basic validation + broadcast)
// ============================================================
void Player::handleMovePlayer(double x, double y, double z, float yaw, float pitch,
                              bool mp_onGround) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (isDead()) return; // congelado hasta ACTION_SPAWN_SAME_DIMENSION (ver Player::respawn)

    // Reject NaN / inf / clearly bogus coordinates.
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return;
    if (!std::isfinite(yaw) || !std::isfinite(pitch)) return;
    // Reject obviously out-of-world Y values.
    if (y < -64.0 || y > 320.0) return;
    // The wire Y is the EYE altitude (vanilla); pos_ is the FEET. Convert
    // once and use the feet value everywhere below so the speed check, the
    // stored position and the fall-distance delta all agree.
    double ny = y - kEyeHeight;
    // Anti-cheat: rough speed cap (~20 blocks/tick = 400 b/s — very generous).
    double dx = x - pos_.x, dy = ny - pos_.y, dz = z - pos_.z;
    double d2 = dx*dx + dy*dy + dz*dz;
    if (d2 > 400.0) {
        // Snap the player back to last known position.
        auto mp = std::make_shared<network::protocol::MovePlayerPacket>();
        mp->eid = runtimeId();
        mp->x = (float)pos_.x; mp->y = (float)(pos_.y + kEyeHeight); mp->z = (float)pos_.z;
        mp->yaw = yaw_; mp->bodyYaw = yaw_; mp->pitch = pitch_;
        mp->mode = network::protocol::MovePlayerPacket::MODE_RESET;
        sendPacket(mp);
        return;
    }

    // Last client-reported ground flag (used by the fall-damage check).
    on_ground_fall_check_ = mp_onGround;
    // PlayerMoveEvent — se dispara en CADA movimiento aceptado (alta
    // frecuencia: hasta ~20/s por jugador). No throttleamos el propio
    // chequeo de cancelacion porque EventManager::call() ya es
    // practicamente gratis cuando nadie escucha ese tipo de evento
    // (solo un lookup de mapa + snapshot vacio); lo que si evitamos es
    // pagar el costo de "revertir" salvo que alguien cancele de
    // verdad. Un listener puede usar esto para, por ejemplo, impedir
    // que un jugador entre a una zona protegida.
    {
        auto ev = event::EventManager::get().fire<event::player::PlayerMoveEvent>(
            this, pos_.x, pos_.y, pos_.z, x, ny, z);
        if (ev.isCancelled()) {
            // Revertir: reenviar la posicion ANTERIOR al cliente,
            // igual que la rama de anti-cheat de mas arriba.
            auto mp = std::make_shared<network::protocol::MovePlayerPacket>();
            mp->eid = runtimeId();
            mp->x = (float)pos_.x; mp->y = (float)(pos_.y + kEyeHeight); mp->z = (float)pos_.z;
            mp->yaw = yaw_; mp->bodyYaw = yaw_; mp->pitch = pitch_;
            mp->mode = network::protocol::MovePlayerPacket::MODE_RESET;
            sendPacket(mp);
            return;
        }
    }

    pos_.x = x; pos_.y = ny; pos_.z = z;
    yaw_ = yaw; pitch_ = pitch;

    // ---- Fall-distance tracking (Infinity processMovement) ----
    // While airborne: descending moves accumulate distance, rising moves
    // (jump/climb) reset the baseline. Ground contact does NOT reset here
    // — the accumulated distance must survive until tickHungerAndDamage()
    // applies the damage exactly once (it resets the accumulator after).
    if (!mp_onGround) {
        if (dy < 0.0) fall_distance_ += (float)(-dy);   // accumulating fall
        else fall_distance_ = 0.0f;                     // rising — baseline reset
    }
    moved_this_tick_ = true;

    // Broadcast MoveEntityPacket to other nearby players (so they see this
    // player move). We don't filter by distance for the MVP.
    //
    // BUG FIX (root cause de "el segundo jugador se cuelga/crashea al
    // conectarse"): el fix anterior en doFirstSpawn() (buscar "root cause
    // of the multiplayer crash/kick") solo anuncia entidades EN el momento
    // del spawn de cada jugador — pero deja una ventana abierta: mientras
    // un jugador está en su fase de streaming de chunks (awaiting_first_
    // spawn_ == true, ANTES de que su propio doFirstSpawn() corra), este
    // bloque seguía mandándole MoveEntityPacket de CUALQUIER otro jugador
    // que se moviera durante esa ventana, sin chequear `other->spawned()`.
    // El cliente de ese jugador recién conectado recibe así un paquete que
    // referencia un eid del que nunca oyó hablar (todavía no le llegó el
    // AddPlayerPacket correspondiente, que recién se manda dentro de SU
    // doFirstSpawn()) — el mismo mecanismo de "entidad desconocida" que ya
    // se documentó como causa de crash del cliente MCPE 0.15.x en el fix
    // de arriba, solo que en un punto distinto del código. Con un segundo
    // jugador ya moviéndose activamente en el mundo (el caso normal), esto
    // se dispara casi con certeza durante la ventana de streaming del que
    // recién entra.
    if (srv_) {
        auto me = std::make_shared<network::protocol::MoveEntityPacket>();
        me->eid = runtimeId();
        me->x = (float)pos_.x; me->y = (float)(pos_.y + kEyeHeight); me->z = (float)pos_.z;
        me->yaw = yaw_; me->headYaw = yaw_; me->pitch = pitch_;
        for (auto& other : srv_->playersSnapshot()) {
            if (other.get() == this) continue;
            if (!other->spawned()) continue;
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
    // PlayerChatEvent se dispara con el mensaje ORIGINAL (con códigos
    // de color) para que un listener pueda reescribirlo antes del
    // broadcast; cancelar el evento evita el broadcast por completo.
    if (srv_) {
        auto ev = event::EventManager::get().fire<event::player::PlayerChatEvent>(
            this, message);
        if (!ev.isCancelled()) {
            srv_->broadcastMessage("<" + cfg_.username + "> " + ev.message);
        }
    }
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

    // BlockBreakEvent — un listener puede cancelarlo (p.ej. protección
    // de zonas) para impedir la rotura sin tocar el resto del pipeline.
    {
        auto ev = event::EventManager::get().fire<event::block::BlockBreakEvent>(
            this, math::Vector3{(double)x, (double)y, (double)z}, (int)blockId);
        if (ev.isCancelled()) return;
    }

    // Set block to air (id=0, data=0).
    mcp_chunk_set_block_full(ch, localX, y, localZ, 0, 0);
    mcp_chunk_recalculate_height_map(ch);
    ch->dirty = true;

    // Block-entity cleanup: si el bloque roto tenía un tile (cofre, horno,
    // cartel) hay que sacarlo del registro. Si no se hace, al re-colocar
    // algo en esa misma posición el tile viejo "revive" con su contenido/
    // estado anterior — el mismo tipo de bug de persistencia que el del
    // cartel. PMMP lo resuelve en Tile::onBreak / Level::useBreakOn.
    if (blockId == block::CHEST || blockId == block::FURNACE ||
        blockId == block::BURNING_FURNACE ||
        blockId == block::SIGN_POST || blockId == block::WALL_SIGN) {
        lvl->removeTile(x, y, z);
    }

    // Redstone: breaking a block can disconnect a circuit or remove a support.
    cursestone::redstone::notifyNeighbors(lvl, srv_, x, y, z);

    // Broadcast UpdateBlockPacket.
    auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
    ubp->x = x;
    ubp->z = z;
    ubp->y = (uint8_t)y;
    ubp->blockId = 0;
    ubp->blockData = 0;
    ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;

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

    // Broadcast only to players who can SEE this chunk — PMMP
    // `Level::getChunkPlayers()`. Deliberately NOT isTrackingChunk():
    // see the note on canSeeBlock() — used_chunks_ is send bookkeeping
    // that gets cleared by teleport/spawn-ACK and goes empty while the
    // client is still rendering, which silently dropped every block
    // change for those players (the "ghost block" bug).
    int32_t pk_cx = x >> 4;
    int32_t pk_cz = z >> 4;
    int broadcastTo = 0, broadcastOk = 0;
    if (srv_) {
        for (auto& other : srv_->playersSnapshot()) {
            if (!other) continue;
            if (!other->canSeeBlock(pk_cx, pk_cz)) continue;
            ++broadcastTo;
            if (!other->spawned()) continue;
            other->sendPacket(ubp);
            other->sendPacket(partEv);
            ++broadcastOk;
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
        // DIAGNOSTICO del bug "los demas no ven el bloque roto":
        // cuantos jugadores quedaron dentro del area de vision del chunk
        // y a cuantos realmente se les mando el UpdateBlockPacket. Si
        // esto dice sent=0 con area>0, el paquete se perdio en el camino;
        // si area=0, el filtro de distancia todavia no cubre al receptor.
        utils::MainLogger::get().info("[BLOCK-SYNC] break at chunk " +
            std::to_string(pk_cx) + "," + std::to_string(pk_cz) +
            " flags=" + hex8(network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY) +
            " -> in_view=" + std::to_string(broadcastTo) +
            " sent=" + std::to_string(broadcastOk));
    }
}

namespace {

// isStairsId — escaleras de MCPE 0.15 / protocolo 84. Cubre las de
// BlockIds.h (53, 67, 108, 109, 114, 156) más las variantes de madera
// que existen en la familia (128, 134-136, 163, 164, 180, 203) por si
// se registran después; un id extra aquí no afecta a nada más.
bool isStairsId(uint8_t id) {
    switch (id) {
        case 53: case 67: case 108: case 109: case 114:
        case 128: case 134: case 135: case 136:
        case 156: case 163: case 164: case 180: case 203:
            return true;
        default:
            return false;
    }
}

// computeVanillaPlacementMeta — metadatos de COLOCACIÓN vanilla para el
// bloque recién puesto, con la misma referencia que usa todo el
// proyecto para el comportamiento de protocolo 84: Nukkit. Las tablas
// de orientación viven en block/BlockOrientation.h (única fuente de
// verdad; antes cada bloque tenía su tabla suelta). Solo orienta los
// bloques que en vanilla lo hacen; el resto conserva baseData tal cual.
//
//   face : cara clickeada (0=bottom..5=east) del UseItemPacket
//   yaw  : yaw del jugador en el momento de colocar
//   hitY : componente Y del punto de impacto (0..1) — sirve para slabs
//          (mitad superior) y trampillas (montada arriba)
uint8_t computeVanillaPlacementMeta(uint8_t blockId, uint8_t baseData,
                                    uint8_t face, float yaw, float hitY) {
    using block::BlockOrientation;
    const uint8_t q = BlockOrientation::quadrant(yaw); // 0=S 1=W 2=N 3=E

    // Escaleras: siempre miran hacia el jugador (Nukkit BlockStairs.place,
    // faces {2,1,3,0}). Las esquinas/uniones las calcula el cliente a
    // partir del meta de los vecinos — el servidor sólo guarda los 2 bits.
    if (isStairsId(blockId)) {
        return BlockOrientation::stairsMeta(q);
    }

    // Slabs / doubleslabs: bits bajos = variante, bit 0x08 = mitad superior
    // cuando el click apunta a la mitad alta del lado de la cara.
    if (blockId == 44 /*SLAB*/ || blockId == block::SLAB_WOOD ||
        blockId == block::DOUBLE_SLAB_ID || blockId == block::DOUBLE_SLAB_WOOD) {
        return (uint8_t)((baseData & 0x07) | ((face != 0 && hitY > 0.5f) ? 0x08 : 0x00));
    }

    switch (blockId) {
        // --- Orientados hacia el jugador (frente = cuadrante) ---
        case block::FURNACE:
        case block::BURNING_FURNACE:
        case block::CHEST:
        case block::DISPENSER:
        case block::DROPPER:
            return BlockOrientation::horizontalFaceMeta(q);

        // --- Pegados a la cara clickeada ---
        case block::LADDER:
        case block::WALL_SIGN:
        case block::STONE_BUTTON:
            return BlockOrientation::ladderMeta(face);
        case block::TORCH:
            return BlockOrientation::torchMeta(face);
        case block::LEVER:
            return BlockOrientation::leverMeta(face, face != 0 && hitY > 0.5f);
        case block::VINE:
            return BlockOrientation::vineMeta(face);

        // --- Trampillas: orientación por yaw + bit 0x04 si se monta arriba ---
        case block::TRAPDOOR:
        case block::TRAPDOOR_IRON:
            return (uint8_t)(BlockOrientation::trapdoorMeta(q) |
                             ((face != 0 && hitY > 0.5f) ? 0x04 : 0x00));

        // --- Yunque: eje este-oeste por defecto, rota con el yaw ---
        case block::ANVIL:
            return BlockOrientation::anvilMeta(q);

        // --- Calabaza / repeater: miran HACIA el jugador (opuesto) ---
        case block::PUMPKIN:
        case block::LIT_PUMPKIN:
        case block::UNPOWERED_REPEATER:
        case block::POWERED_REPEATER:
            return (uint8_t)((baseData & 0x0C) | BlockOrientation::yawToMetaOpposite(yaw));

        // --- Troncos / cuarzo pilar / heno: eje según la cara de click ---
        case block::LOG:
        case 157: /* LOG2 (acacia/dark oak) */
        case block::QUARTZ_BLOCK:
        case block::HAY_BALE:
            return BlockOrientation::axisMeta(face, baseData);

        // Hopper guarda su meta (modo de salida); el resto conserva baseData.
        default:
            return baseData;
    }
}

} // namespace

void Player::handleBlockPlace(int x, int y, int z, uint8_t face,
                              int16_t heldItemId, uint8_t heldCount, uint16_t heldDamage,
                              float fx, float fy, float fz) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (!srv_ || !srv_->getDefaultLevel()) return;

    // ---- Compute placed block position based on face ----
    // This is computed UP FRONT because the flint-and-steel branch below
    // needs placeX/Y/Z to call ItemFlintSteel::onActivate(). Previously this
    // calculation lived at the bottom of the function and the flint-and-steel
    // branch referenced placeX/Y/Z before they were declared, producing
    // "use of undeclared identifier" compile errors on both GCC and Clang.
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

    // Redstone components such as levers and buttons react to a click
    // before normal item/block placement is considered.
    // Agachado no se puede accionar una palanca ni un boton (vanilla).
    if (!sneaking_ && cursestone::redstone::tryInteract(lvl, srv_, x, y, z)) {
        return;
    }

    // ---- REGLA VANILLA DE AGACHADO -------------------------------
    //
    // Mientras el jugador esta agachado (PlayerActionPacket
    // ACTION_START_SNEAK=11, ver handlePlayerAction) NO se puede
    // interactuar con tiles ni con bloques: nada de abrir cofres, hornos
    // ni mesas, ni accionar palancas/botones, ni chispa, ni cubos, ni
    // apagar fuego con la mano. Lo unico permitido es COLOCAR bloques y
    // ATACAR (el ataque va por handleInteract/INTERACT_PACKET, que no
    // pasa por esta funcion, asi que sigue funcionando igual).
    //
    // Es la regla estandar de vanilla, que ni Genisys 0.15.0 ni esta
    // base de codigo implementaban: antes el agachado era puramente
    // cosmético en el cliente de quien se agachaba.
    //
    // `sneakLocked` se captura UNA vez al principio y se consulta en cada
    // rama de interaccion de abajo; la colocacion de bloques NO se toca
    // (una puerta, un piston, un cartel o cualquier bloque 1..255 se
    // siguen colocando normalmente mientras se esta agachado).
    const bool sneakLocked = sneaking_;

    // PlayerInteractEvent (RIGHT_CLICK_BLOCK) — se dispara ANTES de
    // cualquier rama de logica (flint&steel, extincion de fuego,
    // colocacion de bloque). Un listener puede cancelarlo (p.ej.
    // proteccion de zona) para bloquear toda interaccion con este
    // bloque sin tener que duplicar el chequeo en cada rama de abajo.
    // `item` refleja lo que el cliente reporta tener en mano; ids
    // fuera de rango uint16_t (no deberia pasar, heldItemId es
    // int16_t) se truncan de forma segura via el cast explicito.
    //
    // Agachado el evento NO se dispara: la interaccion no llega a
    // ocurrir, asi que anunciarla seria mentirle a los plugins.
    if (!sneakLocked) {
        item::ItemStack heldStack;
        heldStack.id    = (uint16_t)heldItemId;
        heldStack.meta  = (uint8_t)(heldDamage & 0xFF);
        heldStack.count = heldCount;
        auto ev = event::EventManager::get().fire<event::player::PlayerInteractEvent>(
            this, event::player::PlayerInteractEvent::RIGHT_CLICK_BLOCK,
            x, y, z, (int)face, heldStack);
        if (ev.isCancelled()) return;
    }

    // Chest — right-clicking a chest opens its inventory instead of
    // placing a block. This has to sit AFTER the PlayerInteractEvent
    // cancel-check above (so a listener that blocks interaction with
    // this block also blocks opening it) and BEFORE the held-item →
    // placement logic below (a chest should open even if the player is
    // holding a block item — you don't accidentally place a block on
    // top of the chest you're trying to open).
    if (!sneakLocked && lvl->getBlockId(x, y, z) == block::CHEST) {
        openChestAt(x, y, z);
        return;
    }

    // Horno — mismo patrón que el cofre: click derecho abre su UI
    // (su propia ventana de contenedor) en vez de colocar un bloque encima.
    if (!sneakLocked && (lvl->getBlockId(x, y, z) == block::FURNACE ||
                         lvl->getBlockId(x, y, z) == block::BURNING_FURNACE)) {
        openFurnaceAt(x, y, z);
        return;
    }

    // Mesa de crafteo (WORKBENCH 58) — el cliente 0.15.x abre su UI de
    // crafteo 3x3 por su cuenta al click derecho (use-item on block);
    // server-side solo garantizamos que le llegue el CraftingDataPacket
    // con las recetas (se manda una vez en doFirstSpawn). No se abre
    // ninguna ventana server-side: las recetas viajan TODAS en un solo
    // packet global y el cliente filtra big/small según dónde esté
    // crafteando.
    if (!sneakLocked && lvl->getBlockId(x, y, z) == block::WORKBENCH &&
        srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[CRAFT] " + cfg_.username +
            " opened crafting table at " +
            std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
    }

    // ---- Decide what block (if any) to place based on held item ----
    // PMMP-style item→block mapping. Item ids 256+ are tools / buckets /
    // etc. and need special handling. Item ids 1..255 are direct block
    // placements.
    //
    // NOTE: heldItemId is int16_t (not uint8_t) so that item ids > 255
    // (e.g. 259=flint&steel, 325=bucket, 326=water bucket, 327=lava bucket)
    // survive intact. Earlier the parameter was uint8_t which silently
    // truncated them and made the comparisons below tautologically false.
    uint8_t placedBlockId   = 0;     // 0 = no placement
    uint8_t placedBlockData = 0;
    bool    consumeItem     = false; // decrement inventory stack?
    bool    skipPlaceSound  = false; // for fire/water/lava (PMMP doesn't play it)
    (void)heldCount;  // currently unused — kept for API parity with PMMP
    (void)fx; (void)fz;  // hit point X/Z — la orientación vanilla sólo usa fy (slabs/trapdoors)

    if (heldItemId == 0) {
        // Empty hand — no placement. BUT, in MCPE 0.15.x the client also
        // sends USE_ITEM_PACKET with an empty hand when the player
        // right-clicks on a FIRE block to extinguish it (PMMP handles this
        // in Level::useItemOn → Fire::onUpdate(BLOCK_UPDATE_NORMAL)).
        //
        // We replicate that behaviour here: if the clicked target (x,y,z)
        // is a FIRE block, we set it to AIR and broadcast the update.
        // This makes fire removable with a single right-click without
        // needing to break the block underneath it.
        constexpr uint8_t FIRE_ID = 51;
        uint8_t targetId = lvl->getBlockId(x, y, z);
        if (!sneakLocked && targetId == FIRE_ID) {
            // Get the chunk for (x, y, z) to set the block.
            int32_t tcx = x >> 4, tcz = z >> 4;
            if (mcp_chunk_t* tch = lvl->chunks().getChunkIfLoaded(tcx, tcz)) {
                int tlx = x & 15, tlz = z & 15;
                mcp_chunk_set_block_full(tch, tlx, y, tlz, 0, 0);
                tch->dirty = true;
                cursestone::redstone::notifyNeighbors(lvl, srv_, x, y, z);

                // Broadcast UpdateBlockPacket to nearby players.
                auto ubp = std::make_shared<network::protocol::UpdateBlockPacket>();
                ubp->x = x;
                ubp->z = z;
                ubp->y = (uint8_t)y;
                ubp->blockId = 0;
                ubp->blockData = 0;
                ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;
                if (srv_) {
                    for (auto& other : srv_->playersSnapshot()) {
                        if (!other) continue;
                        if (!other->canSeeBlock(tcx, tcz)) continue;
                        other->sendPacket(ubp);
                    }
                }
            }
            return;
        }
        // Not fire — just log "no placement" and bail.
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[BLOCK] " + cfg_.username +
                " attempted place with empty hand — ignored");
        }
        return;
    } else if (heldItemId == 193 || heldItemId == 194 || heldItemId == 195 ||
               heldItemId == 196 || heldItemId == 197 ||
               heldItemId == 324 || heldItemId == 330) {
        // Puerta (cualquier especie de madera, o hierro). Sea cual sea
        // el item exacto (193-197 = specie-doors de MCPE 0.15, cada una
        // con id de bloque propio en el rango <256; 324 = puerta de
        // madera genérica; 330 = puerta de hierro), el bloque colocado
        // en el mundo colapsa siempre a WOODEN_DOOR (64) o IRON_DOOR
        // (71) — ver nota extensa en RedstoneManager.cpp sobre por qué
        // BlockIds.h (que gobierna el Level real) no distingue especies
        // de madera para puertas, a diferencia de ItemConstants.h que sí
        // las distingue como item. Consecuencia visual conocida y
        // documentada: toda puerta de madera se ve como la textura de
        // roble en el cliente, sin importar el item usado para
        // colocarla — la redstone y el toggle funcionan igual para
        // todas, que es lo que importa para esta fase del proyecto.
        //
        // Una puerta ocupa 2 bloques verticales, así que no puede
        // seguir el flujo genérico de "un solo setBlock" de más abajo
        // — se colocan ambas mitades aquí y se retorna, igual que ya
        // hace flint&steel unas líneas arriba.
        uint8_t doorId = (heldItemId == 330) ? item::IRON_DOOR_BLOCK
                                              : item::DOOR_BLOCK;
        // Nota: item::DOOR_BLOCK (64) e item::IRON_DOOR_BLOCK (71) son
        // los MISMOS valores numéricos que block::WOODEN_DOOR y
        // block::IRON_DOOR en BlockIds.h (confirmado) — coherente con
        // que en MCPE 0.15 item id == block id para items de bloque.
        // Se usan las constantes de item:: aquí porque ya estaba
        // incluido ese header; no hace falta incluir block/BlockIds.h
        // sólo por esto.

        // No se puede colocar una puerta si no hay espacio para las dos
        // mitades, o si el bloque debajo de la mitad inferior no es
        // sólido (ninguna verificación de "suelo sólido" existe hoy en
        // el proyecto para ítems en general — ver la comprobación de
        // "existing != 0" más abajo, que es la única guarda de este
        // tipo que ya existe — así que replicamos exactamente ese mismo
        // nivel de rigor: sólo se exige aire en las dos celdas destino,
        // sin exigir soporte sólido debajo, para no introducir una regla
        // más estricta que el resto del sistema de colocación).
        if (placeY + 1 >= 256) return; // la mitad superior se saldría del mundo

        int32_t cxLower = placeX >> 4, czLower = placeZ >> 4;
        mcp_chunk_t* chLower = lvl->chunks().getChunkIfLoaded(cxLower, czLower);
        if (!chLower) return; // mismo criterio que el resto: no generar chunk síncronamente
        int localXLower = placeX & 15, localZLower = placeZ & 15;
        uint8_t existingLower = mcp_chunk_get_block_id(chLower, localXLower, placeY, localZLower);
        if (existingLower != 0) return; // celda inferior ocupada

        // La mitad superior puede caer en un chunk distinto sólo si
        // placeY+1 cruza a otro chunk verticalmente — en este formato
        // los chunks son columnas completas (0-255 de alto), así que
        // cxLower/czLower siguen siendo válidos para y+1 también; no
        // hace falta volver a resolver el chunk.
        uint8_t existingUpper = mcp_chunk_get_block_id(chLower, localXLower, placeY + 1, localZLower);
        if (existingUpper != 0) return; // celda superior ocupada

        // Orientación por hacia dónde mira el jugador al colocar,
        // igual que vanilla real. Conversión yaw→4 direcciones
        // cardinales (fórmula estándar: divide el círculo en 4
        // cuadrantes de 90°, centrados en cada dirección — la misma
        // que usa pistonDirOffset indirectamente vía notifyNeighbors,
        // así que ambos sistemas quedan alineados). Mapeo elegido para
        // ESTE proyecto (esquema propio — ver nota en
        // RedstoneManager.cpp sobre por qué no se replica el legado
        // ambiguo de Java): 0=sur, 1=oeste, 2=norte, 3=este.
        // Orientación vanilla Nukkit (BlockDoor.place): {1,2,3,0} sobre
        // el cuadrante yaw→{S,W,N,E} — tabla única en BlockOrientation.h
        // (reemplaza la fórmula suelta que había aquí).
        uint8_t doorFacing = block::BlockOrientation::doorMeta(
            block::BlockOrientation::quadrant(yaw()));

        uint8_t lowerData = doorFacing & 0x03;       // bits 0-1: orientación; bit 0x04 (abierta) empieza en 0; bit 0x08 (superior) en 0
        uint8_t upperData = 0x08 | (doorFacing & 0x03); // bit 0x08 fijo (es superior); conserva orientación para que ambas mitades sepan hacia dónde mira la puerta sin tener que consultar la otra

        mcp_chunk_set_block_full(chLower, localXLower, placeY, localZLower, doorId, lowerData);
        mcp_chunk_set_block_full(chLower, localXLower, placeY + 1, localZLower, doorId, upperData);
        chLower->dirty = true;

        broadcastBlockChange(srv_, placeX, placeY, placeZ, doorId, lowerData);
        broadcastBlockChange(srv_, placeX, placeY + 1, placeZ, doorId, upperData);

        cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY, placeZ);
        cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY + 1, placeZ);

        if (!(cfg_.gamemode & 1) && inv_) { // survival only
            int held = inv_->heldSlot();
            auto cur = inv_->getItem(held);
            if (!cur.isEmpty()) {
                if (cur.count > 1) { cur.count -= 1; inv_->setItem(held, cur); }
                else { inv_->clear(held); }
            }
        }
        return;
    } else if (heldItemId == 33 || heldItemId == item::STICKY_PISTON) {
        // Pistón normal (33) o pegajoso (item::STICKY_PISTON = 29). El
        // pistón normal no tiene constante con nombre en
        // ItemConstants.h — es una laguna real de ese archivo (nunca se
        // llegó a necesitar antes de esta sesión), así que se usa el
        // literal numérico igual que el resto de este switch ya hace
        // para otros casos especiales (259, 326, 327, 331). El valor 33
        // está confirmado contra BlockIds.h::PISTON_ID.
        //
        // Un solo bloque, pero necesita calcular la dirección por
        // yaw+pitch (a diferencia de la puerta, el pistón SÍ puede
        // apuntar arriba/abajo, no sólo a las 4 direcciones
        // horizontales — igual que vanilla real: mirar claramente hacia
        // arriba o abajo al colocar lo orienta verticalmente).
        uint8_t pistonId = (heldItemId == item::STICKY_PISTON) ? block::STICKY_PISTON : block::PISTON_ID;

        int32_t cxP = placeX >> 4, czP = placeZ >> 4;
        mcp_chunk_t* chP = lvl->chunks().getChunkIfLoaded(cxP, czP);
        if (!chP) return;
        int localXP = placeX & 15, localZP = placeZ & 15;
        uint8_t existingP = mcp_chunk_get_block_id(chP, localXP, placeY, localZP);
        if (existingP != 0) return;

        float pitchDeg = pitch();
        uint8_t pistonFacing;
        if (pitchDeg <= -60.0f) {
            pistonFacing = 1; // mirando marcadamente hacia arriba → pistón apunta arriba
        } else if (pitchDeg >= 60.0f) {
            pistonFacing = 0; // mirando marcadamente hacia abajo → pistón apunta abajo
        } else {
            // Tabla vanilla Nukkit (pistón): {3,4,2,5} sobre el mismo
            // cuadrante yaw→{S,W,N,E} — BlockOrientation.h (antes era una
            // tabla local duplicada con los mismos valores).
            pistonFacing = block::BlockOrientation::pistonMeta(
                block::BlockOrientation::quadrant(yaw()));
        }

        mcp_chunk_set_block_full(chP, localXP, placeY, localZP, pistonId, pistonFacing);
        chP->dirty = true;
        broadcastBlockChange(srv_, placeX, placeY, placeZ, pistonId, pistonFacing);
        cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY, placeZ);

        if (!(cfg_.gamemode & 1) && inv_) {
            int held = inv_->heldSlot();
            auto cur = inv_->getItem(held);
            if (!cur.isEmpty()) {
                if (cur.count > 1) { cur.count -= 1; inv_->setItem(held, cur); }
                else { inv_->clear(held); }
            }
        }
        return;
    } else if (heldItemId == item::SIGN) {
        // Cartel (item 323). El bloque real es SIGN_POST (63) o WALL_SIGN
        // (68) — ninguno es igual al id de item, así que no entra en la
        // rama genérica de "block item" (1..255). Portado de Genisys
        // block/SignPost.php::place():
        //   face 0 (cara de abajo) → no se puede colocar;
        //   face 1 (cara de arriba) → cartel de pie, meta = rotación yaw;
        //   faces 2..5 (lateral)    → cartel de pared, meta = cara clickeada.
        if (face == 0) return;
        if (face == 1) {
            placedBlockId   = block::SIGN_POST;   // 63
            placedBlockData = (uint8_t)(
                ((int)std::floor((yaw() + 180.0f) * 16.0f / 360.0f + 0.5f)) & 0x0F);
        } else {
            placedBlockId   = block::WALL_SIGN;   // 68
            // BlockSignPost.place usa meta = cara clickeada (2..5), igual
            // que la escalera de mano — ladderMeta lo devuelve tal cual.
            placedBlockData = block::BlockOrientation::ladderMeta(face);
        }
        consumeItem = !(cfg_.gamemode & 1);  // survival only
    } else if (heldItemId >= 1 && heldItemId <= 255) {
        // Block item — place it directly. Base data = low 4 bits of
        // heldDamage; encima se aplica la orientación vanilla
        // (computeVanillaPlacementMeta: escaleras hacia el jugador,
        // horno/cofre/dispenser de cara, troncos por eje, etc.).
        placedBlockId   = static_cast<uint8_t>(heldItemId);
        placedBlockData = computeVanillaPlacementMeta(
            (uint8_t)heldItemId, (uint8_t)(heldDamage & 0x0F), face, yaw(), fy);
        consumeItem     = !(cfg_.gamemode & 1);  // survival only
    } else if (heldItemId == 331) {
        // Redstone dust item -> Redstone Wire block (55).
        placedBlockId   = 55;
        placedBlockData = 0;
        consumeItem     = !(cfg_.gamemode & 1);
    } else if (heldItemId == 325) {
        // Bucket (empty) — no placement.
        return;
    } else if (sneakLocked && heldItemId > 255) {
        // REGLA VANILLA DE AGACHADO: cualquier item de id > 255
        // (chispa y ferro = 259, cubo = 325, cubo de agua = 326, cubo de
        // lava = 327) es un USO de item, no una colocacion de bloque, y
        // agachado esta prohibido. Si no lo sacamos aqui caeria en la
        // rama final de "otro item — no placement", que es el mismo
        // resultado pero con un log mucho menos util.
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[SNEAK] " + cfg_.username +
                " tried to use item id=" + std::to_string(heldItemId) +
                " while crouching — blocked (vanilla)");
        }
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
    } else if (heldItemId == 259 && !sneakLocked) {
        // Flint and steel — delegar toda la lógica a ItemFlintSteel::onActivate().
        // Agachado no se puede usar (lo captura la rama sneakLocked de
        // arriba, que devuelve antes de llegar aca).
        // Maneja: verificación de bloque sólido, portal del Nether,
        // colocación de fuego, primer tick de propagación y durabilidad.
        item::ItemFlintSteel::onActivate(lvl, this,
            placeX, placeY, placeZ,  // bloque AIR destino (cara clickeada)
            x, y, z,                 // bloque target (el que recibió el click)
            face);
        return;  // onActivate() ya hace el setBlock + broadcast, no continuar
    } else {
        // Other item (food, tool, etc.) — PMMP would call onItemUse; we no-op.
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[BLOCK] " + cfg_.username +
                " used non-block item id=" + std::to_string(heldItemId) + " — no placement");
        }
        return;
    }

    // ---- Place the block at (placeX, placeY, placeZ) ----
    int32_t cx = placeX >> 4;
    int32_t cz = placeZ >> 4;
    // CRITICAL: use getChunkIfLoaded, NOT getChunk. The latter triggers
    // synchronous chunk generation on the main thread when the player
    // right-clicks into an unloaded chunk, which can stall the tick loop
    // for hundreds of milliseconds (chunk gen + populate). PMMP loads
    // chunks via an async worker pool; we don't have that yet, so for
    // now we just no-op the placement if the chunk isn't resident.
    // The player will see "block didn't place" and can retry; that's
    // strictly better than freezing the whole server.
    mcp_chunk_t* ch = lvl->chunks().getChunkIfLoaded(cx, cz);
    if (!ch) {
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[BLOCK] " + cfg_.username +
                " tried to place block in unloaded chunk (" +
                std::to_string(cx) + "," + std::to_string(cz) + ") — ignored");
        }
        return;
    }

    int localX = placeX & 15;
    int localZ = placeZ & 15;

    // Don't place if target is not air, UNLESS the existing block is FIRE.
    // FIRE is canBeReplaced=true per PMMP, so placing a block on top of
    // a fire should extinguish the fire and put the new block in its place.
    // Without this, the client would locally replace the fire with the new
    // block (because canBeReplaced=true) but the server would keep the fire
    // in its chunk → "ghost fire" that damages entities passing through
    // even though visually it's gone. And breaking the new block would
    // then drop a fire item (because the server still has FIRE there).
    uint8_t existing = mcp_chunk_get_block_id(ch, localX, placeY, localZ);
    constexpr uint8_t FIRE_ID = 51;
    if (existing != 0 && existing != FIRE_ID) return;

    // BlockPlaceEvent — un listener puede cancelarlo (p.ej. protección
    // de zonas) para impedir la colocación sin tocar inventario ni chunk.
    {
        auto ev = event::EventManager::get().fire<event::block::BlockPlaceEvent>(
            this, math::Vector3{(double)placeX, (double)placeY, (double)placeZ},
            (int)placedBlockId);
        if (ev.isCancelled()) return;
    }

    // Set the block in the chunk.
    mcp_chunk_set_block_full(ch, localX, placeY, localZ, placedBlockId, placedBlockData);
    ch->dirty = true;

    // Block-entity: los carteles necesitan un TileSign en el registro del
    // Level para que el texto (y el propio tile) sobreviva al save/reload.
    // PMMP crea el Tile al colocar el bloque; acá se hace explícito. Esto
    // es lo que arregla el "cartel que desaparece al re-ingresar": antes
    // el ítem 323 no colocaba NADA server-side (ni bloque ni tile).
    if (placedBlockId == block::SIGN_POST || placedBlockId == block::WALL_SIGN) {
        auto sign = std::make_shared<tile::TileSign>(
            cursestone::level::Position((double)placeX, (double)placeY, (double)placeZ));
        sign->setCreator(cfg_.uuid);
        lvl->setTile(placeX, placeY, placeZ, sign);
    }

    // Redstone: placing a component may connect or power a neighboring circuit.
    cursestone::redstone::notifyNeighbors(lvl, srv_, placeX, placeY, placeZ);

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
    ubp->flags = network::protocol::UpdateBlockPacket::FLAG_ALL_PRIORITY;

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

    // Same visibility filter as for block break (PMMP `Level::addSound` /
    // `getChunkPlayers`) — distance based, see canSeeBlock().
    int32_t pk_cx = placeX >> 4;
    int32_t pk_cz = placeZ >> 4;
    int placeInView = 0, placeSent = 0;
    if (srv_) {
        for (auto& other : srv_->playersSnapshot()) {
            if (!other) continue;
            if (!other->canSeeBlock(pk_cx, pk_cz)) continue;
            ++placeInView;
            if (!other->spawned()) continue;
            other->sendPacket(ubp);
            // Skip the place sound for fire/water/lava (PMMP behaviour).
            if (!skipPlaceSound) other->sendPacket(sndEv);
            ++placeSent;
        }
    }

    // El cartel recién colocado: sincroniza su BlockEntityData a los que
    // trackean el chunk (PMMP Sign::spawnToAll() al crearse el tile), ya
    // con el bloque difundido, para que el cliente tenga la entidad.
    if (placedBlockId == block::SIGN_POST || placedBlockId == block::WALL_SIGN) {
        auto t = lvl->getTile(placeX, placeY, placeZ);
        if (t) sendBlockEntityData(placeX, placeY, placeZ, *t, nullptr);
    }

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[BLOCK] " + cfg_.username + " placed block at " +
            std::to_string(placeX) + "," + std::to_string(placeY) + "," + std::to_string(placeZ) +
            " (id=" + std::to_string(placedBlockId) + " data=" + std::to_string(placedBlockData) +
            " fromItem=" + std::to_string(heldItemId) + ")");
        // DIAGNOSTICO del bug "colocar/quitar bloques no se ve en
        // multiplayer" — ver el comentario equivalente en handleBlockBreak.
        utils::MainLogger::get().info("[BLOCK-SYNC] place at chunk " +
            std::to_string(pk_cx) + "," + std::to_string(pk_cz) +
            " -> in_view=" + std::to_string(placeInView) +
            " sent=" + std::to_string(placeSent));
    }
}

void Player::handlePlayerAction(int action, int x, int y, int z, int /*face*/) {
    // PMMP: ACTION_START_BREAK=0, ACTION_ABORT_BREAK=1, ACTION_STOP_BREAK=2.
    //
    // BUG ARREGLADO: "que se vea la animacion cuando el bloque se va
    // rompiendo".
    //
    // El crack de rotura solo se lo renderiza a quien INICIO el roto:
    // cada cliente dibuja la animacion localmente cuando su propia
    // EntityAction le llega desde el cliente. Los demas no se enteraban
    // de nada hasta que el bloque desaparecia de golpe.
    //
    // PlayerActionPacket es BIDIRECCIONAL: lleva un campo `eid` que en
    // la direccion servidor->cliente identifica a QUE jugador esta
    // ejecutando la accion. Reenviandole el paquete al resto de
    // jugadores con el eid del que rompe, cada cliente remoto sabe que
    // entidad empieza a romper en (x,y,z) y dibuja el crack.
    //
    // Genisys 0.15 no hace esto (su Player.php:2819 solo usa
    // ACTION_START_BREAK para el fix de fuego en creativo y para firing
    // PlayerInteractEvent), asi que aqui va un paso por delante de la
    // referencia. El paquete en si SI es el de Genisys, byte a byte.
    //
    // Los tres estados se re-difunden: START empieza el crack, ABORT
    // (el jugador dejo el boton) y STOP (termino el anim) lo quitan.
    // Nota: al romperse de verdad el bloque, el UpdateBlockPacket de
    // handleBlockBreak ya lo borra, asi que STOP es sobre todo para el
    // caso de cancelar.
    if (action == network::protocol::PlayerActionPacket::ACTION_START_BREAK ||
        action == network::protocol::PlayerActionPacket::ACTION_ABORT_BREAK ||
        action == network::protocol::PlayerActionPacket::ACTION_STOP_BREAK) {
        if (srv_ && srv_->advancedLog()) {
            utils::MainLogger::get().info("[ACTION] " + cfg_.username + " break action=" +
                std::to_string(action) + " at " + std::to_string(x) + "," +
                std::to_string(y) + "," + std::to_string(z));
        }

        // Chequeo de distancia de Genisys
        // (`$pos->distanceSquared($this) > 10000` -> ignora): evita que
        // un cliente modificado mande cracks en bloques a 100+ bloques.
        double dx = pos_.x - (x + 0.5);
        double dy = pos_.y - (y + 0.5);
        double dz = pos_.z - (z + 0.5);
        double d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > 10000.0 || y < 0 || y >= 256) return;

        auto pa = std::make_shared<network::protocol::PlayerActionPacket>();
        pa->eid    = runtimeId();
        pa->action = action;
        pa->x = x; pa->y = y; pa->z = z;
        int32_t bcx = x >> 4, bcz = z >> 4;
        int32_t sent = 0;
        for (auto& other : srv_->playersSnapshot()) {
            if (!other || other.get() == this) continue;
            if (other->isPendingRemoval()) continue;
            if (!other->spawned()) continue;
            if (!other->canSeeBlock(bcx, bcz)) continue;
            other->sendPacket(pa);
            ++sent;
        }
        if (srv_ && srv_->advancedLog() && action == network::protocol::PlayerActionPacket::ACTION_START_BREAK) {
            utils::MainLogger::get().info("[BREAK-ANIM] " + cfg_.username +
                " started crack at " + std::to_string(x) + "," + std::to_string(y) + "," +
                std::to_string(z) + " -> viewers=" + std::to_string(sent));
        }
        return;
    }
    // Sprint toggle (protocol 84, Infinity PlayerActionPacket):
    // START_SPRINT=9 / STOP_SPRINT=10 — drives the higher exhaustion
    // rate in tickHungerAndDamage().
    //
    // BUG ARREGLADO (era invisible para los demas): sprint y agachado
    // viven en la MISMA propiedad de metadata de la entidad
    // (Entity::DATA_FLAGS = 0: bit 3 = SPRINTING, bit 1 = SNEAKING).
    // Antes solo se guardaba el bool local y jamas se mandaba nada, asi
    // que el otro cliente se quedaba con la pose previa para siempre.
    // Ahora cada cambio reenvia el byte de flags completo a todos.
    if (action == network::protocol::PlayerActionPacket::ACTION_START_SPRINT) {
        if (!sprinting_) { sprinting_ = true;  broadcastPoseFlags(); }
    } else if (action == network::protocol::PlayerActionPacket::ACTION_STOP_SPRINT) {
        if (sprinting_)  { sprinting_ = false; broadcastPoseFlags(); }
    }
    // Crouch / sneak toggle (protocol 84, Genisys 0.15.0
    // PlayerActionPacket.php: ACTION_START_SNEAK = 11,
    // ACTION_STOP_SNEAK = 12).
    //
    // setSneaking() ademas actualiza el estado que usa la regla
    // vanilla de handleBlockPlace(): agachado = no se puede interactuar
    // con tiles ni bloques, solo colocar bloques y atacar.
    if (action == network::protocol::PlayerActionPacket::ACTION_START_SNEAK) {
        setSneaking(true);
    } else if (action == network::protocol::PlayerActionPacket::ACTION_STOP_SNEAK) {
        setSneaking(false);
    }
    if (action == network::protocol::PlayerActionPacket::ACTION_SPAWN_SAME_DIMENSION) {
        respawn();
    }
    // Mesa de crafteo (bloque WORKBENCH=58): el 0.15.x manda
    // ACTION_SET_SLOTS (?6) al abrir la UI de la mesa. No hay estado
    // server-side que mantener (las 336 recetas ya se mandaron en
    // doFirstSpawn), pero lo logueamos para diagnóstico.
    if (srv_ && srv_->advancedLog() && x != 0 && y != 0 && z != 0) {
        auto* lvl = srv_->getDefaultLevel();
        if (lvl && lvl->getBlockId(x, y, z) == block::WORKBENCH) {
            utils::MainLogger::get().info("[CRAFT] " + cfg_.username +
                " used crafting table at " +
                std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
        }
    }
}

// ============================================================
// openChestAt / closeOpenChest — the real chest flow.
//
// TileChest is the single source of truth for a chest's contents
// (see TileChest.h). Level::getTile/setTile is what makes the SAME
// TileChest instance come back every time this position is opened,
// instead of a fresh empty one — so items survive a close+reopen for
// as long as the process keeps running (no disk persistence yet, see
// the comment on Level::getTile in Level.h).
// ============================================================
void Player::openChestAt(int x, int y, int z) {
    if (!srv_ || !srv_->getDefaultLevel()) return;
    auto* lvl = srv_->getDefaultLevel();

    // Only one container open at a time per player (see openContainerWindow).
    if (open_chest_) {
        closeOpenChest();
    }

    // Reuse the existing TileChest at this position, or create one the
    // first time this chest is opened. Level::getTile returns a
    // shared_ptr<tile::Tile> (the base type — the registry can hold any
    // tile kind), so it needs a dynamic_cast back down to TileChest.
    // A null result after a non-null getTile() would mean a different
    // kind of tile-entity is registered at this position, which
    // shouldn't happen for a block we've just confirmed is block::CHEST,
    // but we guard it anyway rather than dereferencing a null pointer.
    std::shared_ptr<tile::TileChest> chest;
    auto existing = lvl->getTile(x, y, z);
    if (existing) {
        chest = std::dynamic_pointer_cast<tile::TileChest>(existing);
        if (!chest) return;
    } else {
        chest = std::make_shared<tile::TileChest>(
            cursestone::level::Position((double)x, (double)y, (double)z));
        lvl->setTile(x, y, z, chest);
    }

    // InventoryOpenEvent — a listener can cancel (e.g. zone protection,
    // a chest mid-trade) to block opening even though PlayerInteractEvent
    // already passed. Nothing is sent to the client and no viewer is
    // registered if this is cancelled.
    auto ev = event::EventManager::get().fire<event::inventory::InventoryOpenEvent>(
        std::static_pointer_cast<inventory::Inventory>(chest->inventory()), this);
    if (ev.isCancelled()) return;

    chest->addViewer();
    open_chest_ = chest;
    openContainerWindow();   // sets container_window_id_ for every packet below

    auto op = std::make_shared<network::protocol::ContainerOpenPacket>();
    op->windowid = container_window_id_;
    op->type     = network::protocol::ContainerOpenPacket::TYPE_CONTAINER;
    op->slots    = (int16_t)chest->inventory()->getSize();
    op->x = x; op->y = y; op->z = z;
    op->entityId = -1;  // block-based container, not entity-based
    sendPacket(op);

    // The client can only draw and open a chest THROUGH its block
    // actor: if the chunk went out before the tile existed (a chest
    // from a village or a structure is just a block until somebody
    // asks for it), the client has no block entity at these
    // coordinates, shows an unknown-but-solid block and refuses to
    // open the UI. Send it now — to everyone looking at the chunk, not
    // just the opener. Level::ensureTilesLoaded covers the normal
    // path (tiles exist before the chunk is sent); this covers the
    // ones created after the fact.
    sendBlockEntityData(x, y, z, *chest, nullptr);

    // Full slot dump so the client actually shows the contents. Can't
    // reuse BaseInventory::sendContents() — it hardcodes windowid=0
    // (correct for the player's own inventory, wrong here: it would
    // collide with SPECIAL_INVENTORY and get treated as a hotbar/main
    // inventory update instead of the chest window).
    auto content = std::make_shared<network::protocol::ContainerSetContentPacket>();
    content->windowid = container_window_id_;
    content->slots.reserve(chest->inventory()->getSize());
    for (int i = 0; i < chest->inventory()->getSize(); ++i) {
        content->slots.push_back(chest->inventory()->getItem(i));
    }
    sendPacket(content);

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[CHEST] " + cfg_.username + " opened chest at " +
            std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
    }
}

void Player::closeOpenChest() {
    // Cubre ambos contenedores (cofre o horno) — solo UNO puede estar
    // abierto a la vez, así que basta con cerrar el que esté seteado.
    if (open_chest_) {
        auto cp = std::make_shared<network::protocol::ContainerClosePacket>();
        cp->windowid = container_window_id_;
        sendPacket(cp);

        open_chest_->removeViewer();
        open_chest_.reset();
    }
    if (open_furnace_) {
        auto cp = std::make_shared<network::protocol::ContainerClosePacket>();
        cp->windowid = container_window_id_;
        sendPacket(cp);

        open_furnace_->removeViewer(this);
        open_furnace_.reset();
    }
    container_window_id_ = 0;
}

// openFurnaceAt — espejo exacto de openChestAt para el horno:
// TileFurnace persistente via Level::getTile/setTile, viewers para el
// progreso del tick, ContainerOpenPacket tipo FURNACE y volcado de
// los 3 slots (input / fuel / output).
void Player::openFurnaceAt(int x, int y, int z) {
    if (!srv_ || !srv_->getDefaultLevel()) return;
    auto* lvl = srv_->getDefaultLevel();

    // Solo un contenedor abierto por jugador.
    if (open_chest_ || open_furnace_) {
        closeOpenChest();
    }

    std::shared_ptr<tile::TileFurnace> furnace;
    auto existing = lvl->getTile(x, y, z);
    if (existing) {
        furnace = std::dynamic_pointer_cast<tile::TileFurnace>(existing);
        if (!furnace) return;   // otro tipo de tile en esa posición
    } else {
        furnace = std::make_shared<tile::TileFurnace>(
            cursestone::level::Position((double)x, (double)y, (double)z));
        lvl->setTile(x, y, z, furnace);
    }

    // InventoryOpenEvent — cancelable (protección de zona, etc.), igual
    // que en openChestAt().
    inventory::Inventory* fInvRaw =
        static_cast<inventory::Inventory*>(furnace->inventory().get());
    auto ev = event::EventManager::get().fire<event::inventory::InventoryOpenEvent>(
        std::shared_ptr<inventory::Inventory>(std::shared_ptr<void>(), fInvRaw), this);
    if (ev.isCancelled()) return;

    furnace->addViewer(this);
    open_furnace_ = furnace;
    openContainerWindow();  // sets container_window_id_ for every packet below

    auto op = std::make_shared<network::protocol::ContainerOpenPacket>();
    op->windowid = container_window_id_;
    op->type     = network::protocol::ContainerOpenPacket::TYPE_FURNACE;
    op->slots    = 3;
    op->x = x; op->y = y; op->z = z;
    op->entityId = -1;
    sendPacket(op);

    // Mismo motivo que en el cofre: sin block entity el cliente 0.15.x
    // no tiene sobre qué montar la UI del horno (y abrirla sin él lo
    // revienta), asi que se le manda antes de cualquier otro paquete
    // de la ventana.
    sendBlockEntityData(x, y, z, *furnace, nullptr);

    // Volcado completo de los 3 slots — mismo motivo que el cofre:
    // BaseInventory::sendContents() hardcodea windowid=0.
    auto content = std::make_shared<network::protocol::ContainerSetContentPacket>();
    content->windowid = container_window_id_;
    content->slots.reserve(3);
    for (int i = 0; i < 3; ++i) content->slots.push_back(furnace->getItem(i));
    sendPacket(content);

    // Estado inicial de flecha/llama para que la UI no arranque en un
    // estado fantasma (ContainerSetDataPacket property 0/1).
    furnace->sendProgressToViewers();

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[FURNACE] " + cfg_.username + " opened furnace at " +
            std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
    }
}

// ============================================================
// openContainerWindow — assign the next window id to the container this
// player just opened and return it.
//
// Infinity hands container ids out from 2 upward in a cycle
// (Player::addWindow: max(2, ++windowCnt % 99)), because 0 is the
// client's own inventory and 0x78/0x79/0x7a are armor/creative/hotbar.
// We copy that range exactly: the old fixed id 1 is never sent by the
// reference server and the client can refuse to open a window it does
// not recognise.
// ============================================================
uint8_t Player::openContainerWindow() {
    container_window_id_ = next_container_window_id_;
    next_container_window_id_ =
        (next_container_window_id_ >= 98) ? 2
                                           : (uint8_t)(next_container_window_id_ + 1);
    return container_window_id_;
}

// ============================================================
// handleCraftingEvent — el flujo real de crafteo (protocolo 84).
//
// Portado del case CRAFTING_EVENT_PACKET de Genisys Player.php (la
// MISMA versión de protocolo que habla este server):
//   1. Buscar la receta por los 16 bytes del UUID (los mismos que
//      escribimos en CraftingDataPacket).
//   2. Verificar que el jugador REALMENTE tiene los ingredientes en
//      su inventario (confianza cero en el packet).
//   3. CraftItemEvent (cancelable).
//   4. Consumir ingredientes y dar el resultado.
//   5. Cerrar la ventana de crafteo y re-sincronizar el inventario
//      con ContainerSetContentPacket windowid=0 — así es como el
//      cliente 0.15.x "ve" el resultado.
// ============================================================
void Player::handleCraftingEvent(
        const std::array<uint8_t, 16>& recipe_uuid,
        const std::vector<cursestone::item::ItemStack>& input,
        const std::vector<cursestone::item::ItemStack>& output,
        uint8_t window_id) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (!inv_ || !srv_) return;
    (void)input; (void)output;  // la verdad está en inv_, no en el packet

    auto* mgr = srv_->craftingManager();
    const inventory::Recipe* recipe =
        mgr ? mgr->getRecipeByUuidBytes(recipe_uuid.data()) : nullptr;
    if (!recipe) {
        // UUID desconocido. Resync del inventario y cerrar la ventana
        // que el cliente dejó abierta (Genisys: sendContents + break).
        utils::MainLogger::get().debug("Player " + cfg_.username +
            " sent unknown crafting recipe UUID");
        if (window_id != 0 && window_id != container_window_id_) {
            auto cp = std::make_shared<network::protocol::ContainerClosePacket>();
            cp->windowid = window_id;
            sendPacket(cp);
        }
        sendInventory();
        return;
    }

    // ---- Conteo de ingredientes requeridos (id + meta) ----
    struct Need { uint16_t id; int16_t meta; int count; };
    std::vector<Need> needs;
    auto addNeed = [&](uint16_t id, int16_t meta, int count) {
        for (auto& n : needs) {
            if (n.id == id && n.meta == meta) { n.count += count; return; }
        }
        needs.push_back({id, meta, count});
    };
    if (recipe->type() == inventory::RECIPE_SHAPED) {
        const auto& g = recipe->grid();
        const auto& m = recipe->gridMeta();
        for (int i = 0; i < 9; ++i) {
            if (g[i] != 0) addNeed(g[i], m[i], 1);
        }
    } else if (recipe->type() == inventory::RECIPE_SHAPELESS) {
        const auto& ing = recipe->ingredients();
        const auto& im = recipe->ingredientMeta();
        for (size_t i = 0; i < ing.size(); ++i) {
            addNeed(ing[i], i < im.size() ? im[i] : (int16_t)-1, 1);
        }
    } else {
        return;  // las recetas de horno no se craftean en la grilla
    }

    // ---- Validar contra el inventario real (slots 0..35) ----
    // meta == -1 en la receta = acepta cualquier damage (equals con
    // anyDamage de PMMP).
    bool canCraft = true;
    std::vector<int> used(inv_->getSize(), 0);
    for (const auto& n : needs) {
        int found = 0;
        for (int slot = 0; slot < inv_->getSize(); ++slot) {
            auto it = inv_->getItem(slot);
            if (it.id != n.id) continue;
            if (n.meta >= 0 && it.meta != (uint16_t)n.meta) continue;
            int avail = it.count - used[slot];
            if (avail <= 0) continue;
            int take = std::min(avail, n.count - found);
            used[slot] += take;
            found += take;
            if (found >= n.count) break;
        }
        if (found < n.count) { canCraft = false; break; }
    }
    if (!canCraft) {
        utils::MainLogger::get().debug("Player " + cfg_.username +
            " lacks ingredients for recipe " + std::to_string(recipe->uuid()));
        sendInventory();
        return;
    }

    // ---- CraftItemEvent (cancelable) ----
    auto ev = event::EventManager::get().fire<event::inventory::CraftItemEvent>(
        this, std::static_pointer_cast<inventory::Inventory>(inv_), *recipe);
    if (ev.isCancelled()) {
        sendInventory();
        return;
    }

    // ---- Consumir ingredientes (marcados en used[]) ----
    for (int slot = 0; slot < inv_->getSize(); ++slot) {
        if (used[slot] == 0) continue;
        auto it = inv_->getItem(slot);
        it.count -= used[slot];
        if (it.count <= 0) it = cursestone::item::ItemStack::EMPTY();
        inv_->setItem(slot, it);
    }

    // ---- Dar el resultado (o dropearlo si no cabe) ----
    auto result = recipe->result();
    if (result.count <= 0) result.count = 1;
    auto leftover = inv_->addItem({result});
    for (const auto& extra : leftover) {
        if (!extra.isEmpty()) {
            dropItem(pos_.x, pos_.y, pos_.z,
                     extra.id, (uint8_t)extra.count, extra.meta);
        }
    }

    // ---- Cerrar la ventana de crafteo + resync (el ack del 0.15.x) ----
    if (window_id != 0 && window_id != container_window_id_) {
        auto cp = std::make_shared<network::protocol::ContainerClosePacket>();
        cp->windowid = window_id;
        sendPacket(cp);
    }
    sendInventory();

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[CRAFT] " + cfg_.username + " crafted id=" +
            std::to_string(result.id) + " count=" + std::to_string(result.count));
    }
}

// handleInventoryCraft — fallback "sin event": cuando el cliente arma
// la receta EN el inventario 2x2 y la ventana se cierra sin que haya
// llegado un CraftingEventPacket, el resultado queda como item fantasma
// en la grilla del cliente. En ese close intentamos matchear la grilla
// 2x2 contra las recetas y, si matchea, corremos el mismo flujo del
// event-path (validar + consumir + dar). Mismo orden de slots que
// sendInventory(): 0..3 = grilla 2x2.
void Player::handleInventoryCraft() {
    if (!inv_ || !srv_) return;
    auto* mgr = srv_->craftingManager();
    if (!mgr) return;

    std::array<uint16_t, 9> grid{};
    std::array<uint16_t, 9> meta{};
    bool any = false;
    for (int i = 0; i < 4; ++i) {
        auto it = inv_->getItem(i);
        if (!it.isEmpty()) { grid[i] = it.id; meta[i] = it.meta; any = true; }
    }
    if (!any) return;

    const inventory::Recipe* r = mgr->matchShapedWithMeta(grid, meta);
    if (!r) {
        std::vector<std::pair<uint16_t, uint16_t>> pairs;  // (id, meta)
        for (int i = 0; i < 4; ++i) {
            if (grid[i] != 0) pairs.push_back({grid[i], meta[i]});
        }
        std::sort(pairs.begin(), pairs.end());
        std::vector<uint16_t> ids, metas;
        ids.reserve(pairs.size());
        metas.reserve(pairs.size());
        for (auto& pr : pairs) { ids.push_back(pr.first); metas.push_back(pr.second); }
        r = mgr->matchShapelessWithMeta(ids, metas);
    }
    if (!r) return;

    handleCraftingEvent(r->uuidBytes(), {}, {}, 0);
}

// ============================================================
// handleInteract — melee combat entry point.
//
// MCPE 0.15.x sends InteractPacket with ACTION_LEFT_CLICK when the
// player attacks an entity (right-click uses ACTION_RIGHT_CLICK for
// things like feeding/taming, which CurseStone doesn't implement yet —
// only the attack path is wired here).
//
// Scope, deliberately minimal:
//   - Only Player targets (srv_->playersSnapshot()) — there are no
//     AI mobs to hit yet (README: "No mob AI (entities exist as data
//     structs only, no pathfinding)").
//   - Flat BASE_ATTACK_DAMAGE for every hit — no per-item damage
//     table (swords/axes) exists in CurseStone yet.
//   - No knockback, no invulnerability-ticks beyond the plain
//     cooldown below, no reach/distance check. This is a foundation
//     for EntityDamageByEntityEvent/PlayerDeathEvent to have
//     something real to fire from, not a feature-complete combat
//     system.
// ============================================================
void Player::handleInteract(int action, int64_t targetEid) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (action != network::protocol::InteractPacket::ACTION_LEFT_CLICK) return;
    if (!srv_) return;
    if (isDead()) return; // un jugador muerto no puede golpear

    // Cooldown server-side anti-spam (no es "attack cooldown" de
    // gameplay como en versiones modernas — ver comentario junto a
    // ATTACK_COOLDOWN_MS en Player.h).
    uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (now_ms - last_attack_ms_ < ATTACK_COOLDOWN_MS) return;

    // Resolver el eid objetivo contra los jugadores conectados. No
    // hay mobs con runtime id todavia, asi que este es el unico
    // universo de objetivos posible.
    std::shared_ptr<Player> target;
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this) continue;
        if (other->runtimeId() == targetEid) { target = other; break; }
    }
    if (!target || !target->spawned() || target->isDead()) return;

    last_attack_ms_ = now_ms;

    // damageByEntity() dispara EntityDamageByEntityEvent internamente
    // (ver Living::damageByEntity) — un listener puede cancelarlo o
    // reescribir la cantidad antes de que se aplique a health().
    bool died = target->damageByEntity(BASE_ATTACK_DAMAGE, this);

    // Sincronizar salud + animacion de golpe al objetivo y a los
    // demas jugadores que lo esten trackeando. No filtramos por
    // distancia/chunk aqui por simplicidad (mismo criterio MVP que
    // el resto de los broadcasts de Player.cpp).
    //
    // BUG FIX (mismo mecanismo que el de MoveEntityPacket en
    // handleMovePlayer): hurtEv->eid referencia la entidad del que
    // recibió el golpe — mandarlo a un jugador que todavía está en su
    // ventana de streaming de chunks (sin spawned() todavía) le llega
    // como un paquete de una entidad que su cliente nunca conoció.
    auto hurtEv = std::make_shared<network::protocol::EntityEventPacket>();
    hurtEv->eid = target->runtimeId();
    hurtEv->event = died ? network::protocol::EntityEventPacket::DEATH_ANIMATION
                         : network::protocol::EntityEventPacket::HURT_ANIMATION;
    for (auto& other : srv_->playersSnapshot()) {
        if (!other) continue;
        if (!other->spawned()) continue;
        other->sendPacket(hurtEv);
    }
    auto sh = std::make_shared<network::protocol::SetHealthPacket>();
    sh->health = (int32_t)target->health();
    target->sendPacket(sh);

    // ---- Knockback (Genisys 0.15.0 Living::knockBack) ----
    //
    // Sin esto, un golpe solo baja la vida: el cliente que recibe el
    // golpe se queda clavado en el sitio y el resto de jugadores ni se
    // enteran. El empujon se calcula sobre la VICTIMA con
    // (deltaX, deltaZ) = (victima - atacante):
    //
    //     $f = sqrt($x*$x + $z*$z);      if($f <= 0) return;
    //     $f = 1 / $f;
    //     $motion->x /= 2;  $motion->y /= 2;  $motion->z /= 2;   // amortigua
    //     $motion->x += $x * $f * $base;
    //     $motion->y += $base;
    //     $motion->z += $z * $f * $base;
    //     if($motion->y > $base) $motion->y = $base;            // tope vertical
    //     $this->setMotion($motion);
    //
    // $base = 0.4 en Genisys Living::attack(), mas 0.15 por nivel de
    // el encantamiento Knockback (Player.php:3144). No hay sistema de
    // encantamientos en este proyecto, asi que va 0.4 fijo, que es
    // exactamente el valor por defecto de Genisys.
    //
    // El motion viaja en SetEntityMotionPacket (Genisys Level::tick()
    // -> motionToSend -> addChunkPacket). Estaba sin implementar (stub),
    // asi que sin esto el empujon no podia existir.
    // Genisys Living::attack() llama a knockBack() SIEMPRE, incluso en el
    // golpe mortal (la animacion de muerte va despues). Se replica.
    {
        const double dx = target->position().x - pos_.x;
        const double dz = target->position().z - pos_.z;
        double f = std::sqrt(dx * dx + dz * dz);
        if (f > 1e-6) {
            f = 1.0 / f;
            constexpr double kBase = 0.4;   // Genisys Living::knockBack default
            // motion actual de la victima, mitigado a la mitad.
            double mx = target->motion().x * 0.5;
            double my = target->motion().y * 0.5;
            double mz = target->motion().z * 0.5;
            mx += dx * f * kBase;
            my += kBase;
            mz += dz * f * kBase;
            if (my > kBase) my = kBase;     // tope vertical de Genisys

            target->setMotion(math::Vector3{mx, my, mz});

            // SetEntityMotionPacket a todos los que ven a la victima
            // (mismo filtro de visibilidad que el resto de difusiones).
            auto motion = std::make_shared<network::protocol::SetEntityMotionPacket>();
            motion->entities.push_back({target->runtimeId(),
                                        (float)mx, (float)my, (float)mz});
            int32_t tcx = (int32_t)std::floor(target->position().x) >> 4;
            int32_t tcz = (int32_t)std::floor(target->position().z) >> 4;
            for (auto& other : srv_->playersSnapshot()) {
                if (!other) continue;
                if (other->isPendingRemoval()) continue;
                if (!other->spawned()) continue;
                if (!other->canSeeBlock(tcx, tcz)) continue;
                other->sendPacket(motion);
            }
            // Se incluye a la propia victima en el bucle a proposito:
            // su cliente NO simula un golpe recibido (no es una accion
            // que el jugador haya ejecutado localmente), asi que sin
            // este SetEntityMotionPacket se quedaria clavada en el sitio
            // mientras los demas si la ven moverse.
        }
    }

    if (srv_->advancedLog()) {
        utils::MainLogger::get().info("[COMBAT] " + cfg_.username + " hit " +
            target->getName() + " for " + std::to_string(BASE_ATTACK_DAMAGE) +
            " (health now " + std::to_string((int)target->health()) + ")" +
            (died ? " — killed" : "") +
            " knockback=0.4");
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
        case 55: return {331, 0};                   // Redstone Wire -> Redstone dust
        case 76: return {75,  0};                   // Redstone Torch (ON) -> OFF item
        case 124: return {123, 0};                  // Redstone Lamp (ON) -> OFF item
        case 20: return {0,   0};                   // Glass -> no drop
        case 79: return {0,   0};                   // Ice -> no drop
        case 89: return {348, 0};                   // Glowstone -> Glowstone Dust
        case 18: return {0,   0};                   // Leaves -> no drop (chance-based)
        case 7:  return {0,   0};                   // Bedrock -> unbreakable
        case 46: return {0,   0};                   // TNT -> no drop
        case 51: return {0,   0};                   // FIRE -> no drop (PMMP Fire::getDrops() = [])
        case 54: return {54,  0};                   // Chest -> chest item
        case 61: return {61,  0};                   // Furnace -> furnace item
        case 62: return {61,  0};                   // Burning Furnace -> furnace item
        case 63: return {323, 0};                   // Standing Sign -> sign item (Genisys SignPost::getDrops)
        case 68: return {323, 0};                   // Wall Sign -> sign item (Genisys WallSign::getDrops)
        default: return {blockId, blockData};       // Default: drop self
    }
}

// ============================================================
// Drop an item entity at a position — pocketmine\level\Level::dropItem()
// (Genisys 0.15), via Item::spawnTo():
//
//     $pk = new AddItemEntityPacket();
//     $pk->eid = $this->getId();  $pk->x/y/z = <exact entity position>
//     $pk->speedX/Y/Z = $this->motionX/Y/Z;
//     $pk->item = $this->getItem();
//     $player->dataPacket($pk);
//
// The default motion is the `(lcg_value()*0.2 - 0.1, 0.2,
// lcg_value()*0.2 - 0.1)` used for block/death drops; a Q drop passes
// getDirectionVector()->multiply(0.4) instead (see handleDropItem).
// ============================================================
int64_t Player::dropItem(double x, double y, double z, uint16_t itemId,
                         uint8_t count, uint16_t damage,
                         double motionX, double motionY, double motionZ,
                         uint32_t pickup_delay) {
    // Level::dropItem() bails on an empty stack.
    if (itemId == 0 || count == 0) return 0;
    if (!srv_) return 0;
    if (motionX == 0.0 && motionY == 0.0 && motionZ == 0.0) {
        // Level::dropItem()'s default: `$motion = new Vector3(
        // lcg_value() * 0.2 - 0.1, 0.2, lcg_value() * 0.2 - 0.1)`. The
        // upward nudge is what makes a broken block's drop pop out of the
        // block it came from instead of sinking into it.
        auto lcg = []() {
            return (double)std::rand() / (double)RAND_MAX;
        };
        motionX = lcg() * 0.2 - 0.1;
        motionY = 0.2;
        motionZ = lcg() * 0.2 - 0.1;
    }

    // Allocate a runtime entity ID (PMMP `Level::$entityIdCounter`).
    int64_t eid = srv_->allocateEid();

    auto pk = std::make_shared<network::protocol::AddItemEntityPacket>();
    pk->eid        = eid;
    pk->itemId     = static_cast<int16_t>(itemId);
    pk->itemCount  = count;
    pk->itemDamage = static_cast<int16_t>(damage);
    // Genisys uses the raw entity position — NOT floor(x)+0.5. Rounding to
    // the block centre is what used to make a dropped stack appear half a
    // block off from where the client thought it was.
    pk->x = (float)x;
    pk->y = (float)y;
    pk->z = (float)z;
    pk->speedX = (float)motionX;
    pk->speedY = (float)motionY;
    pk->speedZ = (float)motionZ;

    // Broadcast only to players tracking this chunk (PMMP `addChunkPacket`).
    int32_t pk_cx = (int)std::floor(x) >> 4;
    int32_t pk_cz = (int)std::floor(z) >> 4;
    for (auto& other : srv_->playersSnapshot()) {
        if (!other) continue;
        if (other.get() == this) {
            // Always send to the dropper too so they see their own drop.
        } else if (!other->canSeeBlock(pk_cx, pk_cz)) {
            continue;
        }
        other->sendPacket(pk);
    }

    // Register the dropped item in the Level's registry so it can be
    // picked up, and so Level::tickDroppedItems() can integrate the very
    // same motion the client is integrating.
    if (srv_->getDefaultLevel()) {
        level::DroppedItem di;
        di.eid        = eid;
        di.itemId     = itemId;
        di.count      = count;
        di.damage     = damage;
        di.x          = x;
        di.y          = y;
        di.z          = z;
        di.spawn_tick = srv_->getCurrentTick();
        di.pickup_delay_ticks = pickup_delay;
        di.taken     = false;
        di.vx        = motionX;
        di.vy        = motionY;
        di.vz        = motionZ;
        // $lastX/$lastY/$lastZ — start at the spawn point so the very
        // first updateMovement() only fires once the item has really moved.
        di.lx = x; di.ly = y; di.lz = z;
        srv_->getDefaultLevel()->addDroppedItem(di);
    }

    if (srv_->advancedLog()) {
        utils::MainLogger::get().info("[DROP] eid=" + std::to_string(eid) +
            " item=" + std::to_string(itemId) + " x" + std::to_string(count) +
            " dmg=" + std::to_string(damage) + " at " +
            std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) +
            " motion=" + std::to_string(motionX) + "," +
            std::to_string(motionY) + "," + std::to_string(motionZ) +
            " delay=" + std::to_string(pickup_delay));
    }
    return eid;
}

// ============================================================
// Entity::getDirectionVector() (Genisys 0.15):
//     $y  = -sin(deg2rad($this->pitch));
//     $xz = cos(deg2rad($this->pitch));
//     $x  = -$xz * sin(deg2rad($this->yaw));
//     $z  =  $xz * cos(deg2rad($this->yaw));
//     return ...->normalize();
// ============================================================
math::Vector3 Player::getDirectionVector() const {
    const double kDegToRad = M_PI / 180.0;
    double y  = -std::sin((double)yaw_ * kDegToRad);
    double xz =  std::cos((double)pitch_ * kDegToRad);
    double x  = -xz * std::sin((double)yaw_ * kDegToRad);
    double z  =  xz * std::cos((double)yaw_ * kDegToRad);
    double len = std::sqrt(x * x + y * y + z * z);
    if (len > 0) { x /= len; y /= len; z /= len; }
    return math::Vector3{x, y, z};
}

// ============================================================
// handleDropItem — portado 1:1 de Genisys Player::handleDataPacket
// case DROP_ITEM_PACKET (protocolo 84):
//
//   if($this->spawned === false or $this->blocked or !$this->isAlive()) break;
//   if(!$this->inventory->contains($packet->item) ...) { sendContents; break; }
//   $slot = $this->inventory->first($packet->item);
//   if($slot == -1) { sendContents; break; }
//   $dropItem = $this->inventory->getItem($slot);
//   $ev = new PlayerDropItemEvent($this, $dropItem);
//   if($ev->isCancelled()) { sendSlot($slot); break; }
//   $this->inventory->remove($dropItem);
//   $motion = $this->getDirectionVector()->multiply(0.4);
//   $this->level->dropItem($this->add(0, 1.3, 0), $dropItem, $motion, 40);
//
// Two deviations on purpose:
//   * Genisys' BaseInventory::remove() wipes EVERY slot holding the same
//     id+meta, so a hotbar with two stacks of cobble loses both while only
//     one entity is spawned. We remove exactly `count` from the slot
//     `first()` found, which is what `removeItem()` is for.
//   * Genisys ignores DropItemPacket::$type and always throws the whole
//     stack. $type is the Q / Ctrl+Q distinction (0 = one, 1 = the stack),
//     so honouring it is what keeps the client's optimistic animation and
//     the server's inventory in agreement.
// ============================================================
void Player::handleDropItem(network::protocol::DropItemPacket& pk) {
    if (!spawned() || !inv_) return;
    if (isDead()) return;   // Genisys: `if(... or !$this->isAlive()) break;`

    item::ItemStack want{};
    want.id    = (uint16_t)pk.itemId;
    want.meta  = (uint8_t)pk.itemDamage;
    want.count = (int)pk.itemCount;
    if (want.isEmpty()) return;

    if (!inv_->contains(want)) { sendInventory(); return; }
    int slot = inv_->first(want);
    if (slot < 0) { sendInventory(); return; }

    item::ItemStack dropped = inv_->getItem(slot);
    if (dropped.isEmpty() || dropped.count <= 0) { sendInventory(); return; }
    auto ev = event::EventManager::get().fire<event::player::PlayerDropItemEvent>(this, dropped);
    if (ev.isCancelled()) { sendSlot(slot); return; }

    // type 1 = "drop the stack" (Ctrl+Q); type 0 = "drop what the packet
    // asked for". Clamp to what the slot actually holds so a spoofed
    // count can never clone or void items.
    int drop_count = (pk.type != 0) ? dropped.count : want.count;
    if (drop_count > dropped.count) drop_count = dropped.count;
    if (drop_count <= 0) { sendInventory(); return; }

    // Take it out of the slot it came from — decrement and only clear the
    // slot when it runs out. BaseInventory::remove() used to erase the map
    // entry while iterating it (SIGSEGV on every Q press); this is the
    // safe single-indexed form. clear()/setItem() each fire onSlotChange,
    // which sends the ContainerSetSlot ack the client needs.
    item::ItemStack left = dropped;
    left.count -= drop_count;
    if (left.count <= 0) {
        inv_->clear(slot);
    } else {
        inv_->setItem(slot, left);
    }

    // $this->level->dropItem($this->add(0, 1.3, 0), $dropItem, $motion, 40)
    //
    // The motion magnitude is 0.15, not Genisys' 0.4. With 0.4 the item
    // is launched at 0.4 blocks/tick with drag 0.02 and, once it lands,
    // keeps sliding at the block friction factor — measured with this
    // exact integrator it comes to rest 3.48 blocks from the player,
    // against a pickup reach of 0.925. So a Q-dropped item ALWAYS landed
    // outside the box the server tests, which is the whole of "a veces
    // puedo y a veces no": you drop it, walk to where you dropped it, and
    // there is nothing there. At 0.15 it comes to rest ~1.3 blocks out and
    // the ground magnet in Level::tickDroppedItems() closes the rest, so
    // the item is always where you dropped it and always collectable.
    const double kMUL = 0.15;
    math::Vector3 dir = getDirectionVector();
    dropItem(pos_.x, pos_.y + 1.3, pos_.z,
             dropped.id, (uint8_t)drop_count, dropped.meta,
             dir.x * kMUL, dir.y * kMUL, dir.z * kMUL,
             40 /* PickupDelay: 2 s, Genisys' manual-drop value */);
}

// ============================================================
// consumeHeldItem — portado de Genisys Player::consumeHeldItem().
// Llamado desde ENTITY_EVENT_PACKET (evento 9, Eating). Sólo los items
// FoodSource tienen efecto; el resto es no-op.
// ============================================================
void Player::consumeHeldItem() {
    if (!spawned() || !inv_) return;
    item::ItemStack held = inv_->getHeldItem();
    if (held.isEmpty()) return;

    item::Item* def = item::Item::get(held.id);
    if (!def || !def->isFood()) return;

    auto ev = event::EventManager::get().fire<event::player::PlayerItemConsumeEvent>(this, held);
    if (ev.isCancelled()) { sendInventory(); return; }

    int restored = def->foodPoints();
    if (restored > 0) {
        int h = foodLevel() + restored;
        setFoodLevel(h > 20 ? 20 : h);
    }
    held.count -= 1;
    inv_->setItemInHand(held.count > 0 ? held : item::ItemStack::EMPTY());
    sendInventory();
}

// ============================================================
// handleSignEdit — portado de Genisys Player::handleDataPacket case
// BLOCK_ENTITY_DATA_PACKET (protocolo 84): edición de cartel.
//   - descarta ediciones a más de 100 bloques;
//   - exige que el tile sea un Sign y que el NBT "id" sea "Sign";
//   - sólo el Creator (UUID del jugador) puede editar;
//   - dispara SignChangeEvent (cancelable) y aplica las líneas.
// ============================================================
void Player::handleSignEdit(network::protocol::BlockEntityDataPacket& pk) {
    if (!spawned() || !srv_) return;
    level::Level* lvl = srv_->getDefaultLevel();
    if (!lvl) return;

    double dx = (double)pk.x - pos_.x;
    double dy = (double)pk.y - pos_.y;
    double dz = (double)pk.z - pos_.z;
    if (dx * dx + dy * dy + dz * dz > 10000.0) return;

    auto tile = lvl->getTile(pk.x, pk.y, pk.z);
    auto sign = std::dynamic_pointer_cast<tile::TileSign>(tile);
    if (!sign) return;
    if (pk.namedtag.empty()) return;

    mcp_tag_t* root = mcp_nbt_read_bytes(pk.namedtag.data(), pk.namedtag.size(),
                                         /*little_endian=*/true);
    if (!root) return;

    std::string id = mcp_compound_tag_get_string(root, "id", "");
    if (id != "Sign") { mcp_tag_destroy(root); return; }

    std::array<std::string, 4> lines = {
        mcp_compound_tag_get_string(root, "Text1", ""),
        mcp_compound_tag_get_string(root, "Text2", ""),
        mcp_compound_tag_get_string(root, "Text3", ""),
        mcp_compound_tag_get_string(root, "Text4", "")
    };
    mcp_tag_destroy(root);

    // Genisys compara el Creator contra el NBT DEL TILE del servidor
    // (`$t->namedtag["Creator"]`), no contra el paquete — el cliente NUNCA
    // manda Creator en su BlockEntityData, así que leerlo del paquete haría
    // que toda edición se cancelara (bug). El dueño es quien colocó el
    // cartel (Player::handleBlockPlace setea el Creator del tile).
    auto ev = event::EventManager::get().fire<event::block::SignChangeEvent>(
        pk.x, pk.y, pk.z, this, lines);
    const std::string& creator = sign->creator();
    if (creator.empty() || creator != cfg_.uuid) ev.setCancelled(true);
    if (ev.isCancelled()) {
        // Genisys: `$t->spawnTo($this)` — reenvía el NBT actual del cartel
        // al editor para revertir el texto que el cliente ya mostró.
        sendBlockEntityData(pk.x, pk.y, pk.z, *sign, this);
        return;
    }

    for (int i = 0; i < 4; ++i) sign->setLine(i, ev.getLine(i));
    // Genisys: Sign::setText() termina en `$this->spawnToAll()` — el cartel
    // se re-spawnea a todos sus viewers para que vean el texto nuevo sin
    // recargar el chunk.
    sendBlockEntityData(pk.x, pk.y, pk.z, *sign, nullptr);
}

// ============================================================
// sendBlockEntityData — serializa el NBT de un tile y lo manda como
// BlockEntityDataPacket. `only` selecciona un único destinatario
// (Genisys Tile::spawnTo), o todos los que trackean el chunk
// (Genisys Tile::spawnToAll). Reutilizable por cualquier block entity.
// ============================================================
void Player::sendBlockEntityData(int32_t x, int32_t y, int32_t z,
                                 tile::Tile& t, Player* only) {
    if (!srv_) return;

    mcp_tag_t* tag = mcp_compound_tag_new();
    if (!tag) return;
    t.writeNBT(tag);
    size_t blen = 0;
    uint8_t* bbuf = mcp_nbt_write_bytes(tag, /*little_endian=*/true, &blen);
    mcp_tag_destroy(tag);
    if (!bbuf || blen == 0) { free(bbuf); return; }

    auto pk = std::make_shared<network::protocol::BlockEntityDataPacket>();
    pk->x = x;
    pk->y = y;
    pk->z = z;
    pk->namedtag.assign(bbuf, bbuf + blen);
    free(bbuf);

    if (only) {
        only->sendPacket(pk);
        return;
    }

    int32_t cx = x >> 4;
    int32_t cz = z >> 4;
    for (auto& other : srv_->playersSnapshot()) {
        if (!other) continue;
        if (other.get() == this || other->canSeeBlock(cx, cz)) {
            other->sendPacket(pk);
        }
    }
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
    //
    // BUG FIX: this only ever pushed the 36 real slots — the comment
    // above already said "45 slots" but the loop below it never matched
    // that. Real PMMP's PlayerInventory::sendContents() (protocol 84 /
    // MCPE 0.15.10) pads 9 extra dummy AIR slots on top of the 36 real
    // ones, with the comment "Because PE is stupid and shows 9 less
    // slots than you send it, give it 9 dummy slots so it shows all the
    // REAL slots." Without that padding, the MCPE 0.15.x client's
    // inventory UI under-renders this container by exactly 9 slots, so
    // some of the real, server-side items in the tail of the 36-slot
    // array never show up on screen even though inv_ has them — the
    // same "item exists server-side but isn't visible client-side"
    // symptom as the creative-drop bug.
    {
        auto pk = std::make_shared<network::protocol::ContainerSetContentPacket>();
        pk->windowid = network::protocol::ContainerSetContentPacket::SPECIAL_INVENTORY;
        pk->slots.reserve(inventory::PlayerInventory::SIZE + 9);
        for (int i = 0; i < inventory::PlayerInventory::SIZE; ++i) {
            pk->slots.push_back(inv_->getItem(i));
        }
        for (int i = 0; i < 9; ++i) {
            pk->slots.push_back(item::ItemStack::EMPTY());
        }
        // Hotbar link indices.
        //
        // BUG REAL ENCONTRADO (comparando contra PlayerInventory::sendContents()
        // real de PMMP protocolo 84 / MCPE 0.15.10):
        //
        //   foreach ($target as $player) {
        //       for ($i = 0; $i < $this->getHotbarSize(); ++$i) {
        //           $index = $this->getHotbarSlotIndex($i);
        //           $pk->hotbar[$i] = $index <= -1 ? -1 : $index + $this->getHotbarSize();
        //       }
        //   }
        //
        // El valor real que va en `hotbar[i]` NO es el índice crudo del slot
        // (0..8) — es ESE índice + 9 (getHotbarSize()). Esto es porque
        // `slots` en el paquete ya trae, primero, los 36 slots reales y
        // LUEGO 9 slots dummy de AIR pegados al final (el padding "PE is
        // stupid and shows 9 less slots" que ya aplicamos arriba) — y el
        // array `hotbar` no apunta al inventario lógico, apunta a la
        // posición DENTRO de ese array `slots` ya paddeado. Por eso el +9:
        // hotbar[i] tiene que señalar la posición real dentro de `slots`
        // donde vive ese item, y las primeras 9 posiciones de esa
        // numeración "con offset" siguen siendo, confusamente, para la UI
        // del hotbar en pantalla — pero el índice que se manda es el de la
        // sección de 36, no el de 0..8 directo.
        //
        // Esta función mandaba `pk->hotbar.push_back(h)` con h=0..8 SIN
        // sumar 9 y SIN consultar el link real (inv_->getHotbarSlotIndex(h)),
        // asumiendo siempre el link identidad. Con eso, el cliente recibe
        // un array hotbar que apunta a las posiciones 0..8 de `slots` en
        // vez de 9..17 — exactamente donde SÍ están los primeros 9 slots
        // reales del inventario (recordar: slots[0..35]=reales,
        // slots[36..44]=dummy AIR). El resultado: el cliente arma su UI de
        // hotbar apuntando a la sección equivocada del array, y sólo por
        // COINCIDENCIA numérica un slot puede llegar a verse bien — el
        // resto queda desincronizado y no refleja los cambios por
        // ContainerSetSlotPacket posteriores (por eso "solo podía sacar del
        // slot que ya tenía algo": ese slot es el único cuyo estado real
        // el cliente ya conocía por otra vía).
        //
        // Fix: usar el link real (inv_->getHotbarSlotIndex(h), normalmente
        // identidad salvo que algo lo haya cambiado) y sumarle
        // PLAYER_INV_HOTBAR_SIZE (9), igual que el PMMP real.
        for (int h = 0; h < inventory::PLAYER_INV_HOTBAR_SIZE; ++h) {
            int idx = inv_->getHotbarSlotIndex(h);
            pk->hotbar.push_back(idx <= -1 ? -1 : idx + inventory::PLAYER_INV_HOTBAR_SIZE);
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
    // BUG FIX: this loop used to stop at PlayerInventory::SIZE (36),
    // which by design EXCLUDES the 4 armor slots (see the comment on
    // PlayerInventory::getSize() — "NOT including armor. The underlying
    // storage has 40 slots"). Armor lives at indices SIZE..SIZE+3
    // (36..39) in the same underlying slots_ map, so the `if (slot >=
    // ARMOR_OFFSET ...)` remap right below was dead code — the loop
    // never reached that range, so equipped armor was silently dropped
    // on every save (disconnect, autosave, /gamemode-triggered save)
    // and players would come back with an empty armor slot every time.
    // Loop over the full 40-slot storage (PLAYER_INV_TOTAL) instead.
    mcp_tag_t* inv_list = mcp_list_tag_new_named("Inventory", MCP_TAG_COMPOUND);
    if (inv_list && inv_) {
        for (int slot = 0; slot < inventory::PLAYER_INV_TOTAL; ++slot) {
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

    // 1. Stream chunks — send up to MAX_CHUNKS_PER_TICK unsent chunks
    // every tick, regardless of whether the player moved. The `moved`
    // check only controls cleanup of out-of-view chunks.
    //
    // BUG FIX: also run this while awaiting_first_spawn_ (i.e. BEFORE
    // spawned_ becomes true), not just after. Chunks need to stream
    // during that window so doFirstSpawn() can be gated on
    // FIRST_SPAWN_CHUNK_THRESHOLD actually having been sent — see the
    // comment in sendChunksAroundSpawn().
    bool awaiting_spawn = awaiting_first_spawn_.load(std::memory_order_relaxed);
    if ((spawned_.load(std::memory_order_relaxed) || awaiting_spawn) && srv_ && srv_->getDefaultLevel()) {
        int cx = (int)pos_.x >> 4;
        int cz = (int)pos_.z >> 4;

        // BUG FIX: Only detect "moved" when the player crosses a CHUNK
        // BOUNDARY (cx/cz changes), not every tick. The old code compared
        // pos with chunk_queue_.front() which triggered "moved=true" on
        // every single tick because the player's exact position floats.
        // This caused used_chunks_ to be cleared and all 64+ chunks to be
        // re-sent from scratch every tick → 1334 FullChunkDataPackets in
        // 2 minutes → client overwhelmed → "Disconnected from server".
        int last_cx, last_cz;
        {
            std::lock_guard<std::mutex> lk(queue_mtx_);
            if (!chunk_queue_.empty()) {
                last_cx = chunk_queue_.front().first;
                last_cz = chunk_queue_.front().second;
            } else {
                last_cx = -999999;
                last_cz = -999999;
            }
        }
        bool moved = (cx != last_cx || cz != last_cz);

        // Compute spiral order for current position (cheap — just coords).
        int vd = cfg_.view_distance;
        auto order = buildChunkSpiral(vd);
        auto& chunks = srv_->getDefaultLevel()->chunks();
        std::unordered_set<int64_t> still_in_view;

        // Send up to MAX_CHUNKS_PER_TICK unsent chunks. This runs EVERY
        // tick, not just when moved — so chunks from sendChunksAroundSpawn()
        // get streamed even though the player hasn't moved yet.
        //
        // BUG FIX (reporte de usuario: "el mundo/terreno no se renderiza y
        // está vacío"): el throttle se había bajado a 1 chunk/tick para
        // arreglar un flood de red, pero nadie ajustó esto en conjunto con
        // view_distance=8 (Player.h) — eso son (2*8+1)^2 = 289 chunks en el
        // área visible. A 1 chunk/tick (50ms) eso son ~14.4 SEGUNDOS para
        // terminar de llenar el área completa. El umbral de primer spawn
        // (FIRST_SPAWN_CHUNK_THRESHOLD=9, un 3x3) sí se cumple rápido y
        // dispara doFirstSpawn() casi al instante — el jugador entra a un
        // mundo donde solo hay un cuadrado de 48x48 bloques cargado
        // alrededor suyo, y el resto del horizonte (hasta donde el cliente
        // MCPE intenta renderizar) se ve vacío/negro durante los
        // siguientes ~14 segundos mientras el resto llega de a 1 por tick.
        // Esto coincide exactamente con lo reportado: no es que el chunk
        // nunca llegue, es que tarda demasiado en llegar comparado con el
        // área que el cliente ya intenta mostrar desde el primer instante.
        //
        // Fix: subir el throttle a un punto medio más razonable. El propio
        // comentario histórico de este bloque decía "usamos 2 como
        // balance" pero el valor real había quedado en 1 — inconsistencia
        // que sugiere que se bajó de más al perseguir el bug del flood.
        // 3 chunks/tick sigue siendo conservador (30-45 fragmentos RakNet
        // por tick de 50ms, dentro de lo que el cliente MCPE 0.15 tolera
        // según el propio análisis de flood de más arriba, que marcó el
        // límite real en 60+ fragmentos en 3ms — un caso muy distinto,
        // ráfaga sin throttle ninguno) y reduce el tiempo de llenado total
        // de ~14.4s a ~4.8s, con margen de sobra para no repetir el
        // desconecte por timeout.
        constexpr int MAX_CHUNKS_PER_TICK = 3;
        int chunks_sent_this_tick = 0;

        // FIX (chunk generation was the real bottleneck, not just the
        // send throttle above): previously this called the BLOCKING
        // chunks.getChunk(chX, chZ), which ran the generator synchronously
        // on the main tick thread — so even discovering that a distant
        // chunk needed generating stalled every player on the server for
        // that tick, and combined with MAX_CHUNKS_PER_TICK=1 it made the
        // world only ever seem to "fill in" wherever the player currently
        // stood, one chunk every 50ms, always starting from the same
        // spiral order.
        //
        // getChunkOrRequestAsync() decouples the two problems:
        //   - GENERATION is requested for every not-yet-loaded chunk in
        //     `order` on THIS tick, regardless of the network throttle
        //     below. Requests fan out across the AsyncPool's worker
        //     threads, so the whole visible radius gets generated in
        //     parallel, in every direction at once — not just wherever
        //     the player happens to be walking.
        //   - SENDING the finished result to this client still respects
        //     MAX_CHUNKS_PER_TICK, since that limit is about not
        //     flooding the MCPE 0.15 client's UDP receive buffer, which
        //     has nothing to do with how fast generation runs.
        auto* pool = srv_ ? srv_->getAsyncPool() : nullptr;
        for (auto& pr : order) {
            int32_t chX = cx + pr.first, chZ = cz + pr.second;
            int64_t hash = ((int64_t)chX << 32) | ((uint32_t)chZ);
            still_in_view.insert(hash);
            bool already_sent;
            { std::lock_guard<std::mutex> lk(chunks_mtx_); already_sent = used_chunks_.count(hash) > 0; }
            if (already_sent) continue;
            // Always request/fetch — cheap and non-blocking — even past
            // the send limit, so generation keeps racing ahead in the
            // background instead of waiting its turn behind the throttle.
            mcp_chunk_t* ch = chunks.getChunkOrRequestAsync(chX, chZ, pool);
            if (!ch) continue;   // not loaded yet — generation requested (or already in flight)
            if (chunks_sent_this_tick >= MAX_CHUNKS_PER_TICK) continue;   // ready, but send is throttled this tick
            size_t outLen = 0;
            uint8_t* buf = mcp_chunk_network_serialize(ch, &outLen);
            if (!buf || outLen == 0) continue;
            auto pk = std::make_shared<network::protocol::FullChunkDataPacket>();
            pk->chunkX = chX; pk->chunkZ = chZ;
            pk->data.assign(buf, buf + outLen);
            free(buf);
            sendPacket(pk);
            sendChunkTiles(chX, chZ);
            { std::lock_guard<std::mutex> lk(chunks_mtx_); used_chunks_.insert(hash); }
            chunks_sent_this_tick++;
        }

        // BUG FIX: once enough chunks have actually reached the client,
        // fire the deferred doFirstSpawn() — this is the
        // FIRST_SPAWN_CHUNK_THRESHOLD gate described in
        // sendChunksAroundSpawn(). Checked every tick while awaiting;
        // doFirstSpawn() itself is idempotent (spawned_.exchange(true)
        // guard) so there's no risk of double-firing.
        if (awaiting_spawn) {
            size_t sent_count;
            { std::lock_guard<std::mutex> lk(chunks_mtx_); sent_count = used_chunks_.size(); }
            if (sent_count >= (size_t)FIRST_SPAWN_CHUNK_THRESHOLD) {
                awaiting_first_spawn_.store(false, std::memory_order_relaxed);
                doFirstSpawn();
            }
        }

        // Only clean up out-of-view chunks when the player actually
        // crossed a chunk boundary.
        if (moved) {
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

    // 2. Hunger + environmental damage (fall/lava/fire/cactus/drown/starve),
    // running every tick. The client owns its own gravity/movement and
    // reports it via MovePlayerPacket; this pipeline only applies the
    // consequences (Infinity foodData + processMovement/checkBlockCollision).
    tickHungerAndDamage(current_tick);
    moved_this_tick_ = false;   // consumed for this tick

    // 2.5. Pickup nearby dropped items — vanilla MCPE 0.15.10 volume.
    //
    // Bedrock collects an item as soon as the item's AABB overlaps the
    // player's AABB grown by ONE block on every axis:
    //
    //   player  [x-0.3, y,     z-0.3] .. [x+0.3, y+1.8, z+0.3]
    //   grow 1  [x-1.3, y-1.0, z-1.3] .. [x+1.3, y+2.8, z+1.3]
    //   item    [ix-0.125, iy, iz-0.125] .. [ix+0.125, iy+0.25, iz+0.125]
    //
    // The geometry lives in one place — level::itemWithinPickupVolume()
    // in Level.h — because Level::tickDroppedItems()'s attraction gate
    // uses the very same test; when the two disagreed an item could sit
    // in a dead ring where it was neither attracted nor collected.
    //
    // WHY THIS CHANGED (the "only pickable inside the hole" report):
    // the volume used to be Genisys' checkNearEntities() one, which grows
    // the box by 0.5 — only 0.8 sideways and 0.5 below the feet. A block
    // broken on the surface drops its item one block BELOW the feet of
    // the player standing next to the hole, so it sat just outside that
    // band and could not be collected; the player had to dig a second hole
    // and step down into it. Vanilla reaches a full block below the feet
    // and 1.3 sideways, which collects items anywhere you can stand next
    // to them (or drop them at your feet).
    if (spawned_.load(std::memory_order_relaxed) && srv_ && srv_->getDefaultLevel() && inv_) {
        auto* lvl = srv_->getDefaultLevel();
        bool any_picked_up = false;

        // The scan is re-run until it stops absorbing. Genisys runs
        // checkNearEntities() once per tick, but every pass here mutates the
        // inventory (merging into an existing stack, filling an empty slot),
        // so the canAddItem()/addItem() verdict for the NEXT entity in the
        // pile is only valid against the state the previous one left behind.
        // One pass per entity is what made a pile hand you a single stack
        // and leave the rest lying there; this drains the whole pile in the
        // tick it comes into reach. Bounded so a full inventory, which fails
        // canAddItem() and reports no progress, cannot spin.
        for (int pass = 0; pass < 32; ++pass) {
        auto dropped = lvl->droppedItems();
        bool picked_this_pass = false;

        for (level::DroppedItem* di : dropped) {
            if (!di || di->taken) continue;
            if (di->pickup_delay_ticks > 0) continue;

            // AxisAlignedBB::intersectsWith(playerBB->grow(1, 1, 1), itemBB)
            if (!level::itemWithinPickupVolume(*di, pos_.x, pos_.y, pos_.z)) continue;

            cursestone::item::ItemStack pickup;
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
            std::vector<cursestone::item::ItemStack> leftovers =
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
                cursestone::item::ItemStack rollback;
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
                //
                // BUG FIX (mismo mecanismo que MoveEntityPacket/
                // EntityEventPacket más arriba): entityId referencia AL
                // JUGADOR que recogió el item — un viewer que todavía
                // esté en su ventana de streaming de chunks (sin
                // spawned() todavía) puede ya estar "trackeando" este
                // chunk (el tracking arranca antes del spawn) sin
                // conocer todavía la entidad de quien recogió el item.
                auto pk_viewer = std::make_shared<network::protocol::TakeItemEntityPacket>();
                pk_viewer->target   = di->eid;
                pk_viewer->entityId = runtimeId();
                auto rm_pk = std::make_shared<network::protocol::RemoveEntityPacket>();
                rm_pk->eid = di->eid;
                int32_t pk_cx = (int32_t)std::floor(di->x) >> 4;
                int32_t pk_cz = (int32_t)std::floor(di->z) >> 4;
                int viewers = 0;
                for (auto& other : srv_->playersSnapshot()) {
                    if (!other) continue;
                    if (other.get() == this) continue;
                    if (other->isPendingRemoval()) continue;
                    if (!other->spawned()) continue;
                    if (!other->canSeeBlock(pk_cx, pk_cz)) continue;
                    other->sendPacket(pk_viewer);
                    // *** BUG ARREGLADO: "los items flotantes no se limpian
                    // en broadcast" ***
                    //
                    // RemoveEntityPacket se mandaba SOLO al que recogió el
                    // item. Los demas recibian TakeItemEntityPacket (que
                    // solo reproduce la animacion de "el item vuela hacia
                    // el jugador") pero NUNCA recibian la orden de borrar
                    // la entidad, asi que su cliente se quedaba con el
                    // item flotando para siempre, ya que el servidor si
                    // lo habia borrado del mundo (removeDroppedItem).
                    //
                    // Genisys: Entity::close() -> Level::removeEntity()
                    // -> RemoveEntityPacket a TODOS los hasSpawned, sin
                    // excluir a quien lo recogio. Por eso el item desaparece
                    // para todos y no solo para el que lo agarro.
                    other->sendPacket(rm_pk);
                    ++viewers;
                }
                auto pk_picker = std::make_shared<network::protocol::TakeItemEntityPacket>();
                pk_picker->target   = di->eid;
                pk_picker->entityId = 0;
                sendPacket(pk_picker);
                lvl->removeDroppedItem(di->eid);
                sendPacket(rm_pk);
                if (srv_ && srv_->advancedLog()) {
                    utils::MainLogger::get().info("[PICKUP] " + cfg_.username +
                        " picked up eid=" + std::to_string(di->eid) +
                        " at chunk " + std::to_string(pk_cx) + "," + std::to_string(pk_cz) +
                        " -> viewers_notified=" + std::to_string(viewers));
                }
            } else {
                // Partial pickup — update the dropped item's count.
                // Reset taken=false so it can be picked up again.
                // Do NOT send TakeItemEntityPacket to viewers — only
                // the picker should know (via inventory update).
                di->count = (uint8_t)leftover_count;
                di->taken = false;
            }

            any_picked_up = true;
            picked_this_pass = true;
            break;   // inventory changed — re-scan for the next entity
        }
        if (!picked_this_pass) break;
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
    // `y` is a FEET coordinate (same convention as teleport()/Genisys'
    // Player::sendPosition(Vector3 $pos)); the wire MovePlayerPacket needs
    // the eye altitude.
    auto pk = std::make_shared<network::protocol::MovePlayerPacket>();
    pk->eid = runtimeId();
    pk->x = (float)x; pk->y = (float)(y + kEyeHeight); pk->z = (float)z;
    pk->yaw = (float)yaw; pk->bodyYaw = (float)yaw; pk->pitch = (float)pitch;
    pk->mode = mode;
    pk->onGround = onGround();
    sendPacket(pk);
}

// --- Nukkit-compat: getSpawn ---
math::Vector3 Player::getSpawn() const {
    if (srv_ && srv_->getDefaultLevel()) return srv_->getDefaultLevel()->spawn();
    return math::Vector3{0, 64, 0};
}

// getServerSpawnSafe — server spawn with Y snapped to the real terrain
// surface of the column (Infinity getSafeSpawn semantics).
//
// FIX ("el spawn está en el cielo la primera vez que entro"): the Normal
// generator's getSpawn() returns Nukkit's static (127.5, 128, 127.5) — a
// Y that is only correct by luck. On most seeds the real surface at that
// column is several blocks lower (or higher), so a first-time player
// started the session floating in the air and fell for seconds.
//
// The fix scans the spawn column top-down for the first block you can
// actually stand on (solid + nothing solid directly above), exactly like
// Infinity's getSafeSpawn: y = highest solid block + 1. If the chunk is
// not loaded (or is all air/void), fall back to the vanilla cloud level.
// X/Z (+0.5 centering) stay exactly as the generator/level.dat defined
// them, so the spawn is deterministic across restarts.
math::Vector3 Player::getServerSpawnSafe() const {
    math::Vector3 sp = getSpawn();
    auto* lvl = srv_ ? srv_->getDefaultLevel() : nullptr;
    if (!lvl) return sp;
    int32_t bx = (int32_t)std::floor(sp.x);
    int32_t bz = (int32_t)std::floor(sp.z);
    auto& chunks = lvl->chunks();
    // getChunk (not getChunkIfLoaded): at respawn-from-afar the spawn
    // chunk may have been unloaded by the view-distance sweep — generate
    // it synchronously (one chunk, boot-class cost) so the safe-Y scan
    // always sees real terrain instead of falling back to Y=64.
    mcp_chunk_t* c = chunks.getChunk(bx >> 4, bz >> 4);
    if (c) {
        for (int y = 127; y > 0; --y) {
            uint8_t id = mcp_chunk_get_block_id(c, bx & 15, y, bz & 15);
            if (id == 0) continue;                                   // air
            if (id == block::LEAVES || id == block::LEAVES_2) continue; // canopy
            // First non-air, non-leaves block: stand on top of it.
            uint8_t above = mcp_chunk_get_block_id(c, bx & 15, y + 1, bz & 15);
            if (above == 0 || above == block::WATER ||
                above == block::FLOWING_WATER) {
                sp.y = (double)y + 1.0;
                return sp;
            }
            // Solid-with-solid-above (tree/overhang): keep scanning down.
        }
    }
    if (sp.y < 1.0) sp.y = 64.0; // vanilla fallback
    return sp;
}

// --- Health sync (Infinity Player::setHealth) -------------------
// Health-only UpdateAttributesPacket: the client moves the hearts the
// instant this packet arrives. Infinity syncs the HEALTH attribute on
// EVERY setHealth() call — without this the hearts only moved on regen
// or food changes and felt stuck for seconds after taking damage.
void Player::sendHealthAttribute() {
    if (!connected_.load(std::memory_order_relaxed)) return;
    auto pk = std::make_shared<network::protocol::UpdateAttributesPacket>();
    pk->eid = runtimeId();
    network::protocol::UpdateAttributesPacket::Entry h;
    h.name = "generic.health";
    h.min = 0.0f; h.max = maxHealth();
    h.value = health();
    h.defaultValue = 20.0f;
    pk->entries.push_back(h);
    sendPacket(pk);
}

// --- Attribute sync (protocol 84 UpdateAttributesPacket) ---
void Player::sendFoodLevel() {
    if (!connected_.load(std::memory_order_relaxed)) return;
    auto pk = std::make_shared<network::protocol::UpdateAttributesPacket>();
    pk->eid = runtimeId();
    network::protocol::UpdateAttributesPacket::Entry e;
    // EXACT name strings from Infinity's Attribute::init (Genisys/PMMP
    // lineage): the client maps the HUD bars by these identifiers —
    // "player.hunger" for the food bar, "generic.health" for hearts.
    e.name = "player.hunger";
    e.min = 0.0f; e.max = 20.0f;
    e.value = (float)foodLevel();
    e.defaultValue = 20.0f;
    pk->entries.push_back(e);
    sendPacket(pk);
}

void Player::sendAllAttributes() {
    if (!connected_.load(std::memory_order_relaxed)) return;
    auto pk = std::make_shared<network::protocol::UpdateAttributesPacket>();
    pk->eid = runtimeId();
    network::protocol::UpdateAttributesPacket::Entry h;
    h.name = "generic.health";
    h.min = 0.0f; h.max = maxHealth();
    h.value = health();
    h.defaultValue = 20.0f;
    pk->entries.push_back(h);
    network::protocol::UpdateAttributesPacket::Entry f;
    f.name = "player.hunger";
    f.min = 0.0f; f.max = 20.0f;
    f.value = (float)foodLevel();
    f.defaultValue = 20.0f;
    pk->entries.push_back(f);
    sendPacket(pk);
}

// Living hook (Living::applyDamage): runs after every non-fatal damage
// application. Mirrors two Infinity behaviours at once:
//   * Player::attack(): EntityEventPacket HURT_ANIMATION with eid=0 —
//     "me" in protocol 84 — so the client flashes red immediately.
//   * Player::setHealth(): sync the HEALTH attribute so the hearts move
//     in the same tick as the damage.
void Player::onHealthChanged() {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (isDead()) return;

    auto ee = std::make_shared<network::protocol::EntityEventPacket>();
    ee->eid = 0;   // protocol 84: eid 0 = "the local player"
    ee->event = network::protocol::EntityEventPacket::HURT_ANIMATION;
    sendPacket(ee);

    sendHealthAttribute();
}

// --- Hunger + ambient damage pipeline (Infinity Player.php foodData/
// processMovement) — runs once per tick for every spawned player. ---
void Player::tickHungerAndDamage(uint64_t current_tick) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (isDead()) return;
    auto* lvl = srv_ ? srv_->getDefaultLevel() : nullptr;
    if (!lvl) return;

    bool creative = isCreative();
    int difficulty = srv_->difficulty();

    uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int bx = (int)std::floor(pos_.x);
    int by = (int)std::floor(pos_.y);
    int bz = (int)std::floor(pos_.z);
    uint8_t at   = lvl->getBlockId(bx, by, bz);
    uint8_t body = lvl->getBlockId(bx, by + 1, bz);   // head block
    uint8_t feet = lvl->getBlockId(bx, by - 1, bz);   // stand-on block

    // ---------- 1. Drowning (Infinity Living::entityBaseTick) ----------
    // Air drains ONLY when the HEAD is submerged: Infinity checks the
    // block at y + getEyeHeight() (isInsideOfWater), NOT the block at the
    // feet. Using the feet block made the server drain air while the
    // client (which shows bubbles by camera height) still had full ones,
    // and kept drowning the player in the surface — so diving again
    // hurt as if the air was already spent.
    int eye_y = (int)std::floor(pos_.y + kEyeHeight);   // player eye height (0.15)
    uint8_t eye = lvl->getBlockId(bx, eye_y, bz);
    bool in_water = (eye == block::WATER || eye == block::FLOWING_WATER);
    if (in_water) {
        if (air_ticks_ > 0) --air_ticks_;
        else if (!creative && difficulty > 0 &&
                 now_ms - last_drown_dmg_ms_ >= 1000) {
            last_drown_dmg_ms_ = now_ms;
            damage(2.0f, event::entity::DamageCause::DROWNING); // 1 heart/s
        }
        // Water extinguishes fire (vanilla) and cancels pending fall damage.
        fire_ticks_ = 0;
        was_on_fire_ = false;
        fall_distance_ = 0.0f;
    } else {
        air_ticks_ = 300;   // 15s of air (vanilla)
    }
    // Un jugador que acaba de morir (p.ej. ahogado) no debe seguir
    // procesando el resto del tick de vida: Genisys Player::onUpdate
    // salta TODO el bloque cuando !isAlive(). Sin esto, la regeneracion
    // de la seccion 4c volvia a curar a un muerto en el mismo tick.
    if (isDead()) return;
    // Burn timer: set to 160 ticks (8s) on fire/lava contact; when it
    // reaches 0 the player stops burning (vanilla setOnFire semantics).
    if (fire_ticks_ > 0 && --fire_ticks_ == 0) was_on_fire_ = false;

    // ---------- 2. Fall damage (Infinity processMovement) ----------
    // Distance is accumulated in handleMovePlayer(); here the damage is
    // applied exactly once when the client reports ground contact with
    // >= 3 blocks of accumulated fall (vanilla: damage = fall - 3), and
    // the accumulator resets so it can start over on the next fall.
    if (!creative && difficulty > 0 && fall_distance_ >= 3.0 &&
        on_ground_fall_check_ && !in_water) {
        damage((float)(fall_distance_ - 3.0), event::entity::DamageCause::FALL);
        fall_distance_ = 0.0f;   // landing resets the fall
    }
    // Si el golpe de caida mato, terminar el tick aca: nada de lo que
    // sigue (fuego/lava/hambre/regen) debe tocar a un jugador muerto.
    if (isDead()) return;

    // ---------- 3. Fire / lava / cactus contact (Infinity checkBlockCollision) ----------
    if (at == block::LAVA || at == block::FLOWING_LAVA) {
        if (now_ms - last_lava_dmg_ms_ >= 500) {
            last_lava_dmg_ms_ = now_ms;
            if (!creative) damage(4.0f, event::entity::DamageCause::LAVA); // 2 hearts
        }
        was_in_lava_ = true;
        was_on_fire_ = true;
        setOnFire(8);
        fire_ticks_ = 160;   // 8s of burning (vanilla)
    } else {
        was_in_lava_ = false;
        if (was_on_fire_ && !creative) {
            // Fire-tick damage while burning (0.5s interval, 1 dmg vanilla).
            if (now_ms - last_fire_tick_dmg_ms_ >= 500) {
                last_fire_tick_dmg_ms_ = now_ms;
                damage(1.0f, event::entity::DamageCause::FIRE_TICK);
            }
        }
    }
    if (at == block::FIRE || body == block::FIRE) {
        if (now_ms - last_fire_dmg_ms_ >= 500) {
            last_fire_dmg_ms_ = now_ms;
            if (!creative) damage(1.0f, event::entity::DamageCause::FIRE);
        }
        was_on_fire_ = true;
        setOnFire(8);
        fire_ticks_ = 160;   // 8s of burning (vanilla)
    }
    if (at == block::CACTUS) {
        if (now_ms - last_cactus_dmg_ms_ >= 500) {
            last_cactus_dmg_ms_ = now_ms;
            if (!creative) damage(1.0f, event::entity::DamageCause::CACTUS); // half heart
        }
    }
    // Lava/fuego/cactus pueden haber matado: no seguir con hambre/regen.
    if (isDead()) return;

    // ---------- 4. Hunger (Infinity foodData) ----------
    if (!creative) {
        // 4a. Accumulate exhaustion from real actions (0.15 values).
        if (moved_this_tick_) {
            food_depletion_ += sprinting_ ? 6 : 1;   // sprint 0.1, walk 0.01 (x100)
        }
        if (in_water) food_depletion_ += 1;
        // 4b. Every 4.0 exhaustion (PMMP/Infinity threshold): drain 1
        // saturation or 1 food, then wait 80 ticks before the next drain.
        // Peaceful difficulty (0) has no food drain / starvation (vanilla).
        if (difficulty >= 1 && food_depletion_ >= 400 && food_tick_timer_ <= 0) {
            food_depletion_ -= 400;
            if (food_saturation_ > 0.0f) {
                food_saturation_ = std::max(0.0f, food_saturation_ - 1.0f);
            } else {
                int h = foodLevel();
                setFoodLevel(h > 0 ? h - 1 : 0);
            }
            food_tick_timer_ = 80;
            sendFoodLevel();
        }
        // 4c. Regen when full (17 food, every 4s) — Infinity handleOrder.
        if (foodLevel() >= 17 && health() < maxHealth() &&
            now_ms - last_regen_ms_ >= 4000) {
            last_regen_ms_ = now_ms;
            heal(1.0f);
            sendAllAttributes();
        }
        // 4d. Starvation at 0 food (1 dmg every 4s). Vanilla floors:
        // easy stops at 10 HP, normal at 1 HP, hard can kill.
        if (difficulty >= 1 && foodLevel() <= 0 && ++starve_ticks_ >= 80) {
            starve_ticks_ = 0;
            float floor_hp = (difficulty >= 3) ? 0.0f
                           : (difficulty == 2) ? 1.0f : 10.0f;
            if (health() > floor_hp) {
                damage(1.0f, event::entity::DamageCause::STARVATION);
            }
        }
    } else {
        // Creative: food is pinned at max, no drain, no starvation.
        if (foodLevel() != 20) { setFoodLevel(20); sendFoodLevel(); }
        food_saturation_ = 5.0f;
        food_depletion_ = 0;
        starve_ticks_ = 0;
    }

    (void)current_tick;
    (void)difficulty;
    (void)feet;
    (void)last_pos_y_;
    (void)last_pos_set_;
    (void)was_in_lava_;
}
void Player::setExperience(int exp, int level) {
    (void)exp; (void)level;
}
void Player::sendExperience(int exp) { (void)exp; }
void Player::sendExperienceLevel(int level) { (void)level; }
void Player::setHealth(int h) { entity::Living::setHealth((uint16_t)h); }
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

} // namespace cursestone
