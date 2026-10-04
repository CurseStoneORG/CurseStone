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
void Player::setMovementSpeed(float speed) { (void)speed; }
void Player::setAttribute(int attrId, float value) { (void)attrId; (void)value; }

} // namespace cursestone
