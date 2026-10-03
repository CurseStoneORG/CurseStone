#include "Player.h"
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

// ============================================================
// Phase 1 — sendPacket: encode a DataPacket and feed it through RakLib
// ============================================================
bool Player::isTrackingChunk(int32_t cx, int32_t cz) const {
    int64_t hash = ((int64_t)cx << 32) | ((uint32_t)cz);
    std::lock_guard<std::mutex> lk(chunks_mtx_);
    return used_chunks_.count(hash) > 0;
}

void Player::sendPacket(std::shared_ptr<network::protocol::DataPacket> p,
                         bool ordered) {
    if (!p) return;
    if (!connected_.load(std::memory_order_relaxed)) return;
    if (!adv_iface_) {
        utils::MainLogger::get().warn("sendPacket: no interface for " + cfg_.username);
        return;
    }

    // Workbench: log outgoing packet.
    WorkbenchManager::get().onPacketOut(cfg_.username, p->pid(), 0);

    // Serialize the packet: [1-byte ID][body].
    std::vector<uint8_t> body;
    p->writePacket(body);
    if (body.empty()) return;

    // Always-on diagnostics: keep the last packets per server so a crash
    // or error report shows what was flowing just before it happened.
    diagnostics::Diagnostics::get().recordPacket(
        /*out=*/true, cfg_.username, p->pid(), body.size());

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

    adv_iface_->sendRawPacket(cfg_.ip, cfg_.port, wire, ordered);
}

void Player::sendMessage(const std::string& msg) {
    auto pk = std::make_shared<network::protocol::TextPacket>();
    pk->type    = network::protocol::TextPacket::TYPE_RAW;
    pk->message = msg;
    sendPacket(pk);
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

// ============================================================
// Helper: giveCreativeItem — "dar" un item tocado en el catálogo
// creativo. Ver el bug real que esto arregla en el comentario dentro
// de CONTAINER_SET_SLOT_PACKET, rama SPECIAL_CREATIVE, más abajo.
//
// No confiamos en `cs->slot`/`cs->hotbarSlot` como índice LITERAL de
// destino — dos intentos previos de interpretar esos campos como "el
// slot exacto donde va" fallaron (el catálogo tiene ~560 entradas, y
// ni tratar `slot` como índice directo ni caer a `hotbarSlot` cuando
// `slot` se sale de rango hizo que el item apareciera del lado del
// cliente). En cambio: el item se DA con addItem() (busca el primer
// slot libre del inventario principal, que es lo que el jugador
// espera — "el item debería ir a mi inventario", no a un slot
// puntual), y la confirmación que se manda de vuelta ECOA el mismo
// windowid/slot/hotbarSlot que mandó el cliente en el paquete
// original — igual que ya se hace para el cofre (ver la rama
// open_chest_ de abajo, que sí confirmó ser confiable). La idea:
// no importa en qué índice interno terminamos guardando el item, lo
// único que el cliente necesita para dejar de mostrar el tooltip y
// reflejar el item es una confirmación que coincida con los mismos
// campos que ÉL mandó — esos son los que su UI está esperando de
// vuelta, sea cual sea el significado exacto que le da internamente.
static void giveCreativeItem(cursestone::Player* player,
                              std::shared_ptr<cursestone::inventory::PlayerInventory> inv,
                              const network::protocol::ContainerSetSlotPacket& cs,
                              cursestone::item::ItemStack it) {
    if (!player || !inv) return;
    // FALLBACK: si el ítem que venía embebido en el paquete llega vacío
    // (id==0), puede que para el gesto "tap en el catálogo" el cliente
    // NO mande el ítem completo — solo `cs.slot` como índice DENTRO del
    // catálogo que el propio servidor ya mandó antes por
    // ContainerSetContentPacket (ver sendCreativeInventory() /
    // inventory::getCreativeItems()), esperando que el servidor sepa
    // qué item es ese índice. No hay riesgo en probar esto primero: si
    // `it` ya traía datos válidos, esta rama ni se toca.
    if (it.id == 0) {
        const auto& catalog = cursestone::inventory::getCreativeItems();
        if (cs.slot >= 0 && (size_t)cs.slot < catalog.size()) {
            it = catalog[(size_t)cs.slot];
        }
    }
    if (it.id == 0) return;
    auto leftover = inv->addItem({it});
    bool given = leftover.empty() || leftover[0].count < it.count;

    auto confirm = std::make_shared<network::protocol::ContainerSetSlotPacket>();
    confirm->windowid   = cs.windowid;
    confirm->slot       = cs.slot;
    confirm->hotbarSlot = cs.hotbarSlot;
    if (given) {
        confirm->itemId     = (int16_t)it.id;
        confirm->itemCount  = it.count;
        confirm->itemDamage = (int16_t)it.meta;
    } else {
        confirm->itemId = 0;  // inventario principal lleno — no se pudo dar
    }
    player->sendPacket(confirm);
}

// ============================================================
// Phase 2 — handleDataPacket: dispatch by packet ID
// ============================================================
void Player::handleDataPacket(network::protocol::DataPacket& p) {
    if (!connected_.load(std::memory_order_relaxed)) return;
    uint8_t pid = p.pid();

    // Workbench: track last activity time + log incoming packet.
    last_activity_ms_ = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    WorkbenchManager::get().onPacketIn(
        cfg_.ip + ":" + std::to_string(cfg_.port), pid, 0);

    // Always-on diagnostics: record the incoming packet and set the current
    // "action" so a crash/error dump shows exactly which packet was being
    // handled when things went wrong (crashdumps before this showed neither).
    diagnostics::Diagnostics::get().recordPacket(
        /*out=*/false, cfg_.username, pid, 0);

    // Wrap the dispatch in try/catch so a bug in a single packet handler can
    // never take the whole server down silently. On exception we log a full
    // report (crashdumps/error_*.json) and kick just the offending player
    // with the error in the disconnect message, like Nukkit does.
    try {
    switch (pid) {
        case network::protocol::LOGIN_PACKET:
            if (auto* lp = dynamic_cast<network::protocol::LoginPacket*>(&p)) {
                handleLogin(*lp);
            }
            break;
        case network::protocol::PLAY_STATUS_PACKET:
            // Client → server spawn ACK. The 0.15.x client answers our
            // PlayStatus(PLAYER_SPAWN) with its own PLAYER_SPAWN packet
            // once it has actually finished loading into the world. This
            // is the ONLY reliable signal that the client is rendering —
            // and the root-cause fix for "sometimes chunks never render":
            // every FullChunkDataPacket sent BEFORE this ACK can be
            // silently discarded by the client (it hasn't finished arming
            // its world renderer yet), while used_chunks_ already marked
            // those chunks as sent, so they were never re-sent. The world
            // then shows a permanent hole exactly where the pre-ACK burst
            // went. On ACK: clear the sent-set and re-stream everything
            // in view — chunks the client kept are cheap no-ops for it,
            // missing ones now arrive and fill the hole.
            if (auto* ps = dynamic_cast<network::protocol::PlayStatusPacket*>(&p);
                ps && ps->status == network::protocol::PlayStatusPacket::PLAYER_SPAWN) {
                handleClientSpawnAck();
            }
            break;
        case network::protocol::MOVE_PLAYER_PACKET:
            if (auto* mp = dynamic_cast<network::protocol::MovePlayerPacket*>(&p)) {
                handleMovePlayer(mp->x, mp->y, mp->z, mp->yaw, mp->pitch, mp->onGround);
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
                //
                // NOTE: itemId is int16_t in the packet (values 256..327 are
                // valid item ids like 259=flint&steel, 325=bucket, 326=water
                // bucket, 327=lava bucket). Casting to uint8_t would silently
                // truncate them and break those items forever, so we pass the
                // full int16_t value through.
                handleBlockPlace(ui->x, ui->y, ui->z, ui->face,
                                 ui->itemId,
                                 ui->itemCount, ui->itemDamage,
                                 ui->fx, ui->fy, ui->fz);
            }
            break;
        case network::protocol::REMOVE_BLOCK_PACKET:
            if (auto* rb = dynamic_cast<network::protocol::RemoveBlockPacket*>(&p)) {
                handleBlockBreak(rb->x, rb->y, rb->z);
            }
            break;
        case network::protocol::MOB_EQUIPMENT_PACKET:
            // Client tells us which hotbar slot the player is now holding.
            //
            // Protocol 84 quirk (Genisys 0.15.10): a plain hotbar tap
            // sends slot == selectedSlot == 0..8 (raw hotbar index), but
            // equipping an item from the *inventory screen* sends
            // slot = full inventory index (9..35) with selectedSlot = the
            // hotbar row it belongs to. Only the selectedSlot byte is the
            // real held-slot for the 9 hotbar positions.
            //
            // BUG REAL (arreglado acá): solo se llamaba setHeldSlot(),
            // que por debajo hace setHotbarSlotIndex(itemInHandIndex_,
            // slot). Como itemInHandIndex_ nunca se actualizaba, cada
            // cambio de slot reescribia hotbar_[0] con el indice nuevo:
            //   - el item EN MANO nunca cambia de verdad, y
            //   - hotbar_[0] y hotbar_[n] quedan apuntando al mismo slot
            //     del inventario (links duplicados).
            // Genisys 0.15.0 (Player.php, case MOB_EQUIPMENT_PACKET)
            // hace DOS llamadas y en este orden:
            //     setHeldItemIndex($packet->selectedSlot, false);
            //     setHeldItemSlot($packet->slot);
            // El primero es justamente lo que fija que slot de la mano
            // estas sosteniendo; el segundo solo rearma el link cuando el
            // cliente REORDENO items (screen de inventario), y aca eso ya
            // lo hace el handler de ContainerSetSlot — asi que aca solo
            // actualizamos el indice en mano.
            if (auto* me = dynamic_cast<network::protocol::MobEquipmentPacket*>(&p)) {
                if (inv_) {
                    // selectedSlot es un byte: el cliente puede mandar
                    // 0..255. Solo 0..8 es un indice de hotbar valido;
                    // si no, caemos al otro byte (hotbarSlot) y, si
                    // tampoco sirve, dejamos el que ya teniamos.
                    int selected = (int)me->selectedSlot;
                    if (selected > 8) selected = (int)me->hotbarSlot;
                    if (selected >= 0 && selected <= 8 &&
                        selected != inv_->getHeldItemIndex()) {
                        inv_->setHeldItemIndex(selected);
                    }
                    if (srv_ && srv_->advancedLog()) {
                        utils::MainLogger::get().info("[INV] " + cfg_.username +
                            " switched to hotbar slot " +
                            std::to_string(inv_->getHeldItemIndex()));
                    }
                }
            }
            break;
        case network::protocol::ANIMATE_PACKET:
            // Client swung its arm (mining/punching/placing). PMMP
            // ignores this server-side; we rebroadcast it to nearby
            // players so arm swings are visible between clients. The
            // client renders its own swing locally, so we exclude the
            // origin player — same broadcast pattern as item pickups.
            if (auto* an = dynamic_cast<network::protocol::AnimatePacket*>(&p)) {
                an->eid = runtimeId();
                auto fwd = std::make_shared<network::protocol::AnimatePacket>(*an);
                if (srv_) {
                    const auto& pos = position();
                    int32_t an_cx = (int32_t)pos.x >> 4;
                    int32_t an_cz = (int32_t)pos.z >> 4;
                    for (auto& other : srv_->playersSnapshot()) {
                        if (!other || other.get() == this) continue;
                        if (other->isPendingRemoval() || !other->spawned()) continue;
                        if (!other->isTrackingChunk(an_cx, an_cz)) continue;
                        other->sendPacket(fwd);
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
            //   - windowid == container_window_id_ (2..98): the chest or
            //     furnace this player currently has open (see
            //     openChestAt/openFurnaceAt/closeOpenChest).
            //     Routed to open_chest_->inventory() instead of inv_.
            if (auto* cs = dynamic_cast<network::protocol::ContainerSetSlotPacket*>(&p)) {
                if (inv_) {
                    cursestone::item::ItemStack it;
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

                    // Chest routing — short-circuits BEFORE the
                    // player-inventory InventoryClickEvent below, which
                    // only knows about SPECIAL_INVENTORY/ARMOR/CREATIVE/
                    // HOTBAR. A click in the currently-open chest fires
                    // its own InventoryClickEvent against the chest's
                    // ChestInventory (not inv_), applies it there, and
                    // confirms the slot with the container's window id —
                    // sendSlot() below can't be reused for this, it
                    // always confirms against inv_ (the player's own
                    // inventory).
                    if (cs->windowid == container_window_id_ && open_chest_) {
                        if (open_chest_ && cs->slot >= 0 &&
                            cs->slot < open_chest_->inventory()->getSize()) {
                            auto chestInv = open_chest_->inventory();
                            auto cev = event::EventManager::get().fire<event::inventory::InventoryClickEvent>(
                                std::static_pointer_cast<inventory::Inventory>(chestInv),
                                this, (int)cs->slot, it);
                            if (!cev.isCancelled()) {
                                chestInv->setItem(cs->slot, it);
                            }
                            // Confirm the slot either way — same reason
                            // as the player-inventory path below: MCPE
                            // 0.15.x visually reverts a slot that never
                            // gets a confirmation packet back, whether
                            // the write actually landed or was cancelled.
                            auto confirm = std::make_shared<network::protocol::ContainerSetSlotPacket>();
                            confirm->windowid = container_window_id_;
                            confirm->slot = cs->slot;
                            confirm->hotbarSlot = cs->slot;
                            auto cur = chestInv->getItem(cs->slot);
                            confirm->itemId     = (int16_t)cur.id;
                            confirm->itemCount  = cur.count;
                            confirm->itemDamage = (int16_t)cur.meta;
                            sendPacket(confirm);
                        }
                        break;
                    }

                    // Horno — misma mecánica que el cofre: click en la
                    // ventana del horno (container_window_id_) escribe
                    // directo en el TileFurnace (slots 0=input, 1=fuel,
                    // 2=output) y
                    // confirma el slot para que el cliente no lo revierta.
                    // Al cambiar el input/fuel, el tick del horno se
                    // encarga de reencender fuel o resetear cook_time.
                    if (cs->windowid == container_window_id_ && open_furnace_) {
                        if (open_furnace_ && cs->slot >= 0 && cs->slot < 3) {
                            auto f = open_furnace_;
                            inventory::Inventory* fInvRaw =
                                static_cast<inventory::Inventory*>(f->inventory().get());
                            auto cev = event::EventManager::get().fire<event::inventory::InventoryClickEvent>(
                                std::shared_ptr<inventory::Inventory>(std::shared_ptr<void>(), fInvRaw),
                                this, (int)cs->slot, it);
                            if (!cev.isCancelled()) {
                                f->setItem(cs->slot, it);
                            }
                            auto confirm = std::make_shared<network::protocol::ContainerSetSlotPacket>();
                            confirm->windowid = container_window_id_;
                            confirm->slot = cs->slot;
                            confirm->hotbarSlot = cs->slot;
                            auto cur = f->getItem(cs->slot);
                            confirm->itemId     = (int16_t)cur.id;
                            confirm->itemCount  = cur.count;
                            confirm->itemDamage = (int16_t)cur.meta;
                            sendPacket(confirm);
                        }
                        break;
                    }

                    // InventoryClickEvent — cubre el flujo de "click en el
                    // inventario propio del jugador" (windowid 0x00/0x78/
                    // 0x7a/0x79). El caso de cofre u horno (windowid ==
                    // container_window_id_) ya se resolvio y salio
                    // arriba. Un listener puede
                    // cancelar para impedir el cambio de slot (p.ej.
                    // inventario bloqueado durante un trade).
                    bool clickCancelled = false;
                    {
                        auto ev = event::EventManager::get().fire<event::inventory::InventoryClickEvent>(
                            std::static_pointer_cast<inventory::Inventory>(inv_),
                            this, (int)cs->slot, it);
                        clickCancelled = ev.isCancelled();
                    }
                    if (clickCancelled) {
                        // Revertir: reenviar el slot actual (sin aplicar el
                        // cambio) para que el cliente no quede desincronizado.
                        // sendSlot() espera el indice ABSOLUTO del inventario;
                        // SPECIAL_INVENTORY y SPECIAL_HOTBAR ya mandan ese
                        // indice directo (0..44 / 0..8), SPECIAL_ARMOR
                        // necesita +ARMOR_OFFSET, y SPECIAL_CREATIVE no
                        // mapea a un slot real (window de solo-lectura) asi
                        // que no hay nada que revertir ahi.
                        using CSS = network::protocol::ContainerSetSlotPacket;
                        if (cs->windowid == CSS::SPECIAL_ARMOR) {
                            sendSlot(inventory::PlayerInventory::ARMOR_OFFSET + cs->slot);
                        } else if (cs->windowid == CSS::SPECIAL_INVENTORY ||
                                   cs->windowid == CSS::SPECIAL_HOTBAR) {
                            sendSlot(cs->slot);
                        }
                        break;
                    }

                    if (cs->windowid == network::protocol::ContainerSetSlotPacket::SPECIAL_INVENTORY) {
                        // windowid 0x00 — main inventory slots 0..44.
                        //
                        // CORREGIDO contra el Player.php REAL de PocketMine
                        // Genisys (protocolo 84, MCPE 0.15.10 — confirmado
                        // por CURRENT_PROTOCOL=84 y MINECRAFT_VERSION_NETWORK
                        // ="0.15.10" en su propio código, no supuesto). Su
                        // manejo de CONTAINER_SET_SLOT_PACKET para windowid=0
                        // es:
                        //
                        //   if ($packet->slot >= $this->inventory->getSize()) break;
                        //   $transaction = new BaseTransaction($this->inventory, $packet->slot, $packet->item);
                        //   ...
                        //   // BaseTransaction::execute():
                        //   if (!$source->isCreative()) { /* validar contra floating inventory */ }
                        //   $this->getInventory()->setItem($this->getSlot(), $this->getTargetItem(), false);
                        //
                        // Es decir: NO hay campo "hotbarSlot" en uso, NO hay
                        // índice de catálogo — el cliente manda `slot` como
                        // el índice REAL de destino (0..44) e `item` con los
                        // datos completos del ítem tocado, tanto para un
                        // arrastre normal como para un tap del catálogo
                        // creativo. Para un jugador en creativo, el servidor
                        // hace setItem(slot, item) directo, sin validar
                        // contra nada — exactamente lo que hace este bloque
                        // ahora. Mis dos intentos anteriores (tratar
                        // `hotbarSlot` como destino, o caer a buscar
                        // `cs->slot` en el catálogo cuando parecía "fuera de
                        // rango") partían de una premisa equivocada: un
                        // `slot` fuera de rango nunca debería llegar de un
                        // cliente real para este windowid, así que ya no se
                        // intenta "adivinar" nada — se ignora, igual que
                        // hace el propio PMMP (`break`).
                        if (cs->slot >= 0 && cs->slot < inventory::PlayerInventory::SIZE) {
                            inv_->setItem(cs->slot, it);
                            // If the player is creative and the slot is in
                            // the hotbar range (0..8), link it as the held
                            // slot so the client shows the item in hand.
                            if ((cfg_.gamemode & 1) != 0 && it.id != 0 &&
                                cs->slot >= 0 && cs->slot < 9) {
                                inv_->setHeldSlot(cs->slot);
                            }
                            // NOTE: no manual sendSlot() here on purpose.
                            // inv_->setItem() already ran
                            // PlayerInventory::onSlotChange(), which sends
                            // the confirmation ContainerSetSlotPacket
                            // itself (see PlayerInventory.cpp). Calling
                            // sendSlot() again here used to send a SECOND,
                            // redundant confirmation for every single
                            // inventory click — harmless to the client but
                            // pure waste. Same fix applies to the two
                            // branches below.
                        }
                    } else if (cs->windowid == network::protocol::ContainerSetSlotPacket::SPECIAL_ARMOR) {
                        // windowid 0x78 — armor slot 0..3 → store at 36..39.
                        if (cs->slot >= 0 && cs->slot < 4) {
                            int dest = inventory::PlayerInventory::ARMOR_OFFSET + cs->slot;
                            inv_->setItem(dest, it);
                        }
                    } else if (cs->windowid == network::protocol::ContainerSetSlotPacket::SPECIAL_HOTBAR) {
                        // windowid 0x7a — BUG REAL DEL "no puedo sacar
                        // items del creativo en slots distintos al que ya
                        // tenia algo".
                        //
                        // El Player.php REAL de Genisys/PMMP (protocolo 84,
                        // MCPE 0.15.10) NO TIENE NINGUNA RAMA para
                        // windowid=0x7a en absoluto: solo trata 0 (inventario)
                        // y SPECIAL_ARMOR (0x78); cualquier otro windowid no
                        // registrado en $this->windowIndex cae al `else`
                        // final y SE IGNORA COMPLETAMENTE (rompe con "the
                        // client sent a transaction for a window which the
                        // server doesn't think they have open").
                        //
                        // 0x7a es solo el link hotbar-index -> slot real que
                        // el cliente reenvia (con id=0/count=0) cada vez que
                        // hace login o resincroniza su hotbar — el propio log
                        // lo confirma: los "wid=0x7a" solo aparecen una vez
                        // por slot justo al hacer login, todos con id=0. NO
                        // es el canal que usa el cliente para pedir un item
                        // del catalogo creativo: para eso siempre manda
                        // windowid=0x00 (ver la rama SPECIAL_INVENTORY, que
                        // es donde de hecho llegan TODOS los toques de
                        // creativo en el log, incluido el slot 0 que sí
                        // funciona).
                        //
                        // El bug: esta rama SÍ escribía el item directo en
                        // inv_ vía setItem() cuando el windowid=0x7a traía un
                        // item real, sin pasar por onSlotChange() de la
                        // misma forma "canonica" en la que el cliente espera
                        // la confirmacion (aunque sendSlot() siempre
                        // devuelve windowid=0x00, el cliente ya habia
                        // marcado ese slot internamente como "actualizado
                        // via 0x7a" y no lo redibuja hasta el proximo toque
                        // real por 0x00 — que es justo lo que hacia parecer
                        // que "solo podes reemplazar el item que ya estaba
                        // ahi": el slot 0 nunca pasaba por 0x7a con un item
                        // real porque ya tenia contenido desde survival, asi
                        // que siempre entraba directo por 0x00 y funcionaba;
                        // cualquier OTRO slot del hotbar, al estar vacio,
                        // recibia su primer toque de creativo por 0x7a y se
                        // quedaba "atascado" hasta que por accidente ya
                        // tuviera contenido real.
                        //
                        // Fix: igual que el PMMP real, 0x7a se ignora como
                        // canal de escritura de items. No hacemos nada aqui
                        // — el toque real del catalogo creativo siempre
                        // llega tambien (o en su lugar) por windowid=0x00,
                        // que sí es el que aplica el cambio correctamente.
                    } else if (cs->windowid == network::protocol::ContainerSetSlotPacket::SPECIAL_CREATIVE) {
                        // windowid 0x79 — el Player.php real de Genisys NO
                        // tiene ningún caso especial para este windowid en
                        // absoluto dentro de CONTAINER_SET_SLOT_PACKET: cae
                        // en su rama "the server doesn't think they have
                        // this window open" y se ignora. Es decir, un
                        // cliente MCPE 0.15.x real NO manda esto para el tap
                        // del catálogo — manda windowid=0x00 (ver arriba).
                        // Se deja esta rama solo como red de seguridad
                        // inofensiva por si algún cliente/variante sí lo
                        // manda; no debería ejecutarse en el caso normal.
                        giveCreativeItem(this, inv_, *cs, it);
                    }
                }
            }
            break;
        // Packets we acknowledge but don't act on for the MVP.
        case network::protocol::BATCH_PACKET:
        case network::protocol::PLAYER_INPUT_PACKET:
        // Genisys: `case ProtocolInfo::SET_HEALTH_PACKET: //Not used` — el
        // cliente lo manda como confirmación de vida, el servidor lo ignora.
        case network::protocol::SET_HEALTH_PACKET:
            break;
        case network::protocol::CONTAINER_CLOSE_PACKET:
            // Client closed whatever container UI it had open (X button,
            // Esc, walking too far away). We only ever track ONE open
            // container per player (open_chest_), so we don't need to
            // check the packet's windowid against it — if the client is
            // telling us "a container closed" and we have one open, it's
            // this one. closeOpenChest() is itself a no-op if nothing is
            // open, so this is safe even for a stray/duplicate packet.
            //
            // Fix del craft-en-inventario: cuando la ventana que se
            // cierra es la 0 (inventario del jugador) y NO hay ningún
            // otro contenedor abierto, el cliente pudo haber dejado un
            // craft 2x2 pendiente (armó la receta y cerró sin mandar
            // CraftingEventPacket). Intentamos resolverlo server-side
            // ANTES de considerar cerrado el flujo.
            if (!open_chest_ && !open_furnace_) {
                handleInventoryCraft();
            }
            closeOpenChest();
            break;
        case network::protocol::CRAFTING_EVENT_PACKET: {
            // El flujo real de crafteo MCPE 0.15.x: el cliente arma la
            // grilla, y al tomar el resultado manda ESTE packet con el
            // UUID de la receta + los items que sacó/puso. El server
            // valida contra el CraftingManager, consume ingredientes del
            // inventario real y da el resultado. Ver handleCraftingEvent()
            // (portado 1:1 de Genisys Player.php case
            // CRAFTING_EVENT_PACKET, protocolo 84).
            if (auto* ce = dynamic_cast<network::protocol::CraftingEventPacket*>(&p)) {
                handleCraftingEvent(ce->id, ce->input, ce->output, ce->windowId);
            }
            break;
        }
        case network::protocol::INTERACT_PACKET:
            if (auto* ip = dynamic_cast<network::protocol::InteractPacket*>(&p)) {
                handleInteract(ip->action, ip->target);
            }
            break;
        case network::protocol::ENTITY_EVENT_PACKET:
            // Genisys case ENTITY_EVENT_PACKET: sólo el evento 9 (Eating)
            // tiene efecto real — consumeHeldItem(). El resto se ignora.
            if (auto* ee = dynamic_cast<network::protocol::EntityEventPacket*>(&p)) {
                if (spawned() && ee->event == 9 /*Eating*/) {
                    consumeHeldItem();
                }
            }
            break;
        case network::protocol::DROP_ITEM_PACKET:
            if (auto* dp = dynamic_cast<network::protocol::DropItemPacket*>(&p)) {
                handleDropItem(*dp);
            }
            break;
        case network::protocol::BLOCK_ENTITY_DATA_PACKET:
            // Edición de cartel (cliente→servidor). Portado de Genisys
            // case BLOCK_ENTITY_DATA_PACKET: valida que el tile sea un
            // Sign, verifica el Creator contra el UUID del jugador y
            // dispara SignChangeEvent (cancelable).
            if (auto* be = dynamic_cast<network::protocol::BlockEntityDataPacket*>(&p)) {
                handleSignEdit(*be);
            }
            break;
        case network::protocol::ITEM_FRAME_DROP_ITEM_PACKET:
            // Genisys sólo actúa si el tile en (x,y,z) es un ItemFrame.
            // CurseStone todavía no implementa item frames (no hay
            // TileItemFrame), así que — igual que Genisys cuando el tile
            // no es un frame — no hay nada que soltar. El paquete queda
            // reconocido y decodificado en vez de "ID desconocido".
            if (auto* ifr = dynamic_cast<network::protocol::ItemFrameDropItemPacket*>(&p)) {
                // no-op por ahora (sin TileItemFrame). Se referencia para
                // evitar el warning de variable no usada.
                (void)ifr;
            }
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
    } catch (const std::exception& e) {
        handlePacketError(pid, std::string("std::exception: ") + e.what());
    } catch (...) {
        handlePacketError(pid, "unknown exception (non-std) while handling packet");
    }
}

// Central handler for an exception thrown while dispatching a packet.
// Writes an always-on report (players + recent packets + errors) and kicks
// the affected player with the error visible on their disconnect screen.
void Player::handlePacketError(uint8_t pid, const std::string& what) {
    char hex[8];
    std::snprintf(hex, sizeof(hex), "%02x", pid);
    const std::string summary =
        "Internal server error handling packet 0x" + std::string(hex) +
        " from " + cfg_.username + ": " + what;

    // Never let diagnostics itself throw out of the catch block.
    try {
        diagnostics::Diagnostics::get().writeReport(
            "exception", summary,
            "class=Player::handleDataPacket pid=0x" + std::string(hex));
    } catch (...) {}

    // Nukkit-style: surface the error to the player instead of a bare kick.
    try {
        kick("Internal server error",
             "The server hit an error while processing your action and the "
             "error was logged. Details: " + what);
    } catch (...) {
        close("server error");
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
    // Cast to unsigned char: ::tolower with a negative value (char is
    // signed on x86 Linux, unsigned on ARM/Android) is undefined
    // behaviour and can index outside glibc's ctype table.
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return (char)::tolower(c); });
    if (lower == "rcon" || lower == "console") {
        close("disconnectionScreen.invalidName");
        return;
    }

    // 4. Skin sanity check (PMMP requires 64*32*4 or 64*64*4 bytes for
    // the LEGACY skin format used by MCPE 0.15.x). Any client sending a
    // skin outside those two exact sizes — which in practice is most
    // modern MCPE-compatible clients/launchers, since they default to
    // newer Persona/geometry-based skin formats or re-encoded PNGs of
    // other dimensions — used to hit this branch and just `return;`
    // here without ever finishing the login handshake.
    //
    // ROOT CAUSE of "connects but gets kicked/times out immediately":
    // that early return skipped EVERYTHING after it — logged_in_ was
    // never set to true, no PlayStatusPacket was ever sent, and
    // close()/kick() were never called either. The client was left
    // waiting forever for a server response to its LoginPacket that
    // was simply never coming. RakLib's own session-timeout logic
    // (no application traffic for N seconds) eventually killed the
    // connection from the outside, which is exactly what shows up on
    // the client as "connected, then kicked/timed out almost
    // instantly" — the log message ("...accepting") was a lie: nothing
    // was actually accepted, the function just silently gave up.
    //
    // Fix: a non-standard skin size is not a reason to abort the
    // handshake at all — just don't trust pk.skinData verbatim and
    // fall back to a safe default skin, then keep going through the
    // rest of the normal login flow below (whitelist/ban/dup-session/
    // NBT/spawn), exactly like PMMP does (it clamps/replaces invalid
    // skin data but never drops the connection over it).
    size_t sl = pk.skinData.size();
    if (sl != 64*32*4 && sl != 64*64*4) {
        utils::MainLogger::get().warn(
            "Non-standard skin size (" + std::to_string(sl) +
            " bytes) from " + cfg_.username + " — using default skin");
        pk.skinData.assign(64*32*4, 0);
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
            std::transform(on.begin(), on.end(), on.begin(),
                           [](unsigned char c) { return (char)::tolower(c); });
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

                // FIX ("el spawn está en el cielo"): first-time players come
                // from either the ctor default or a stale level.dat spawn Y.
                // Snap to the real surface of the spawn column before the
                // position is used for StartGame/chunk streaming. Returning
                // players keep their saved PosX/PosY/PosZ — only the
                // first-join path snaps to terrain.
                if (srv_ && srv_->getDefaultLevel()) {
                    pos_ = getServerSpawnSafe();
                }
            }
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
                        // Legacy CurseStone layout (armor at 36..39).
                        dest_slot = nbt_slot;
                    }
                    // BUG FIX (reporte de usuario: "me equipo armadura, me
                    // desconecto, y al volver a entrar ya no la tengo"):
                    // saveData() SÍ guarda la armadura correctamente
                    // (Slot 100..103, recorriendo hasta PLAYER_INV_TOTAL=40
                    // — ver el comentario "BUG FIX" en saveData() más abajo,
                    // eso ya estaba arreglado). El problema estaba acá, en
                    // la carga: dest_slot para armadura se calcula como
                    // ARMOR_OFFSET + (nbt_slot-100) = 36, 37, 38 o 39 — pero
                    // la condición de abajo exigía
                    // `dest_slot < inventory::PlayerInventory::SIZE`, y SIZE
                    // vale 36 POR DISEÑO (ver el comentario en
                    // PlayerInventory::getSize(): "NOT including armor").
                    // Es decir, cualquier dest_slot de armadura (36..39) es
                    // por definición >= SIZE, así que este `if` los
                    // rechazaba SIEMPRE — setItem() nunca se llamaba para
                    // la armadura leída del NBT, aunque el propio dato ya
                    // estaba bien guardado en disco. Por eso el log muestra
                    // "Loaded inventory for X (3 items)" sin la armadura
                    // equipada: el conteo son los items de inventario
                    // normal que sí pasaban el chequeo, la armadura se leía
                    // del NBT pero se descartaba acá mismo.
                    //
                    // Fix: comparar contra el tamaño REAL del storage
                    // subyacente (PLAYER_INV_TOTAL = 40, que sí incluye los
                    // 4 slots de armadura), no contra SIZE (36, que los
                    // excluye a propósito para getSize()/getInventory()
                    // públicos).
                    if (dest_slot >= 0 &&
                        dest_slot < inventory::PLAYER_INV_TOTAL) {
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
    // BUG FIX: this packet and the ones through SetHealth below are sent
    // ordered=true (RELIABLE_ORDERED on their own channel). The real MCPE
    // client requires this exact sequence (PlayStatus → StartGame →
    // SetTime → SetSpawnPosition → SetDifficulty → SetHealth) to arrive
    // in order; previously everything went out RELIABLE with no ordering
    // at all, so normal network jitter could deliver these out of
    // sequence and silently break the client, eventually causing a
    // "Disconnected from server" a few minutes into the session (see the
    // longer explanation in RakLibInterface.cpp's raklibThreadLoop).
    auto ps = std::make_shared<network::protocol::PlayStatusPacket>();
    ps->status = network::protocol::PlayStatusPacket::LOGIN_SUCCESS;
    sendPacket(ps, true);

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
    sendPacket(sg, true);

    // SetTimePacket.
    if (srv_ && srv_->getDefaultLevel()) {
        auto st = std::make_shared<network::protocol::SetTimePacket>();
        st->time = srv_->getDefaultLevel()->time();
        st->started = true;
        sendPacket(st, true);
    }

    // Weather sync — players joining mid-weather get the full reset+set
    // pair (same packets Infinity's sendWeather sends on join), with the
    // strength carried in `data` so the 0.15.10 client actually renders it.
    if (srv_ && srv_->getDefaultLevel()) {
        srv_->getDefaultLevel()->sendWeatherTo(this);
    }

    // SetSpawnPosition.
    if (srv_ && srv_->getDefaultLevel()) {
        auto sp = srv_->getDefaultLevel()->spawn();
        auto ssp = std::make_shared<network::protocol::SetSpawnPositionPacket>();
        ssp->x = (int32_t)sp.x; ssp->y = (int32_t)sp.y; ssp->z = (int32_t)sp.z;
        sendPacket(ssp, true);
    }

    // SetDifficulty / SetHealth.
    if (srv_) {
        auto sd = std::make_shared<network::protocol::SetDifficultyPacket>();
        sd->difficulty = srv_->difficulty();
        sendPacket(sd, true);
    }
    {
        auto sh = std::make_shared<network::protocol::SetHealthPacket>();
        sh->health = (int32_t)health();
        sendPacket(sh, true);
    }

    utils::MainLogger::get().info("Player " + cfg_.username + " logged in (" +
        cfg_.ip + ":" + std::to_string(cfg_.port) + ")");

    // Stream chunks and trigger first spawn.
    sendChunksAroundSpawn();
}

// ============================================================
// Phase 5 — sendChunksAroundSpawn (spiral order, like PMMP)
// ============================================================
// Fix #3: Spiral algorithm extracted to buildChunkSpiral() — single
// source of truth shared by sendChunksAroundSpawn() and onUpdate().
// Fix #4: sendChunksAroundSpawn() no longer sends chunks directly.
// It only populates chunk_queue_ with spiral coordinates. The actual
// streaming happens in onUpdate() at MAX_CHUNKS_PER_TICK=4 per tick.
// ============================================================

// Vanilla-style square spiral: `radius` is now a real per-axis radius,
// not the old "vd*vd total points" size. This produces every offset in
// [-radius, radius] on both axes — (2*radius+1)^2 points total — same
// shape vanilla/PMMP use for view-distance, ordered center-outward so
// the closest (most important) chunks are generated/sent first.
//
// FIX (see chat diagnosis): the previous version took `vd` as the TOTAL
// point count and looped `i < vd*vd`, which only ever reached about
// vd/2 chunks out on each axis — half the configured view-distance,
// and Server.cpp had to compensate with a hardcoded `viewDistance()/2`
// for the unload sweep. That compensation is removed now that this
// returns a true radius.
std::vector<std::pair<int32_t,int32_t>>
Player::buildChunkSpiral(int radius) {
    std::vector<std::pair<int32_t,int32_t>> order;
    if (radius <= 0) { order.emplace_back(0, 0); return order; }
    int side = 2 * radius + 1;
    order.reserve((size_t)side * (size_t)side);
    int layer = 1, leg = 0, x = 0, z = 0;
    order.emplace_back(x, z);
    int total = side * side;
    for (int i = 1; i < total; ++i) {
        switch (leg) {
            case 0: ++x; if (x == layer) ++leg; break;
            case 1: ++z; if (z == layer) ++leg; break;
            case 2: --x; if (-x == layer) ++leg; break;
            case 3: --z; if (-z == layer) { leg = 0; ++layer; } break;
        }
        order.emplace_back(x, z);
    }
    return order;
}

void Player::sendChunksAroundSpawn() {
    if (!srv_ || !srv_->getDefaultLevel()) {
        doFirstSpawn();
        return;
    }
    int centerX = (int)pos_.x >> 4;
    int centerZ = (int)pos_.z >> 4;
    int vd = cfg_.view_distance;

    // Fix #3: use shared buildChunkSpiral() instead of inline copy.
    auto order = buildChunkSpiral(vd);

    // Fix #4: ONLY populate chunk_queue_ — do NOT send chunks directly.
    // The streaming happens in onUpdate() at MAX_CHUNKS_PER_TICK=4/tick.
    // This prevents blocking the main thread for 64 chunks at login.
    {
        std::lock_guard<std::mutex> lk(queue_mtx_);
        chunk_queue_.clear();
        for (auto& pr : order) {
            chunk_queue_.emplace_back(centerX + pr.first, centerZ + pr.second);
        }
    }

    // BUG FIX ("no puedo sacar items del creativo la primera vez que
    // entro, pero sí funciona si salgo y vuelvo a entrar"): this used to
    // call doFirstSpawn() immediately here — before a single chunk had
    // been streamed — on the theory that "the client will see chunks
    // arriving gradually" (true for Nukkit, which doesn't gate spawn on
    // chunks). But doFirstSpawn() is also where PLAYER_SPAWN,
    // sendCreativeInventory() and sendInventory() (the player's OWN
    // inventory ContainerSetContentPacket) go out. Sending those with
    // zero terrain loaded works fine for a RETURNING player, because
    // their spawn chunks are already generated/cached and stream in
    // almost instantly regardless — but for a brand-new player at a
    // never-before-visited spawn point, chunk GENERATION is async and
    // can take noticeably longer (see getChunkOrRequestAsync() in
    // onUpdate()). The MCPE 0.15.x client doesn't fully "arm" its own
    // inventory window until it has actually finished loading into the
    // world, so an inventory-content packet sent this early can be
    // silently dropped/ignored while the client is still on the loading
    // screen. The server-side item is still added to inv_ (that's why
    // it "appears" on the next login, once the full inventory resync at
    // doFirstSpawn() lands on a client that's already loaded) but the
    // client never rendered the confirmation the first time.
    //
    // Real PMMP never has this race: doFirstSpawn() there is only
    // called once chunkLoadCount >= spawnThreshold (see Player.php's
    // orderChunks()) — i.e. once a handful of chunks around the player
    // have actually reached the client. We replicate that gate instead
    // of spawning immediately: onUpdate() now streams chunks BEFORE
    // spawned_ is true too, and only calls doFirstSpawn() once
    // FIRST_SPAWN_CHUNK_THRESHOLD chunks have gone out.
    awaiting_first_spawn_.store(true, std::memory_order_relaxed);
}

// ============================================================
// Phase 6 — doFirstSpawn
// ============================================================

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
    ubp->flags = 0xFF;
    int32_t cx = x >> 4, cz = z >> 4;
    for (auto& player : server->playersSnapshot()) {
        if (!player) continue;
        if (!player->isTrackingChunk(cx, cz)) continue;
        player->sendPacket(ubp);
    }
}

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

    // Record join time for workbench disconnect diagnostics.
    start_time_ms_ = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    last_activity_ms_ = start_time_ms_;

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

    // CraftingDataPacket — la lista COMPLETA de recetas (shaped,
    // shapeless y furnace). Se manda ANTES de PLAYER_SPAWN, igual que
    // Infinity/Genisys (Server::onPlayerLogin → sendRecipeList antes de
    // que el cliente salga de la pantalla de carga): algunos clientes
    // 0.15 aplican el cleanRecipes y dejan de aceptar la lista si llega
    // después del spawn. Es un packet global, no por-ventana: el
    // cliente MCPE 0.15.x filtra qué recetas muestra según la UI que
    // esté usando (inventario 2x2 vs mesa 3x3). Sin esto el cliente
    // muestra la grilla pero ningún resultado y el click en el slot de
    // resultado no produce nada.
    if (srv_) {
        auto cd = std::make_shared<network::protocol::CraftingDataPacket>();
        if (auto* mgr = srv_->craftingManager()) {
            cd->addAllRecipes(*mgr);
            utils::MainLogger::get().info("Sent " +
                std::to_string(mgr->size()) + " recipes to " + cfg_.username);
        }
        sendPacket(cd);
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

    // Attribute sync for the freshly-spawned player (protocol 84): makes
    // the hunger bar appear in the HUD at the server-side value.
    sendAllAttributes();

    // BUG FIX ("toco un item del creativo y no llega a mi inventario, pero
    // si me desconecto y reconecto ya está ahí, y de ahí en más sí
    // funciona"): sendCreativeInventory() de arriba solo manda el CATÁLOGO
    // de solo-lectura (windowid SPECIAL_CREATIVE). El inventario PROPIO del
    // jugador (windowid SPECIAL_INVENTORY/ARMOR/HOTBAR) nunca se mandaba
    // completo al entrar — de hecho setGamemode() más arriba en este mismo
    // archivo ya tiene un comentario que dice "same as what already happens
    // once at doFirstSpawn()" asumiendo que esta llamada ya existía acá,
    // pero nunca se había escrito.
    //
    // Sin un ContainerSetContentPacket inicial para esas ventanas, el
    // cliente nunca las "abre"/inicializa del lado suyo — así que cuando
    // después llega un ContainerSetSlotPacket puntual (la confirmación de
    // un tap en el catálogo creativo, ver giveCreativeItem()), no tiene
    // ningún contenedor conocido al cual aplicarle ese cambio incremental,
    // y lo ignora en silencio. El item SÍ quedaba guardado en inv_ del
    // lado del servidor (por eso aparecía recién al reconectar, cuando el
    // inventario ya no estaba vacío) pero nunca se veía reflejado en esa
    // primera sesión.
    //
    // Fix: mandar el inventario propio completo acá también, antes de que
    // el jugador tenga oportunidad de interactuar con nada.
    sendInventory();

    // Broadcast a PlayerListPacket (ADD) with all currently-online players.
    //
    // BUG FIX: mismo criterio que el AddPlayerPacket de abajo — si otro
    // jugador está en su propia ventana de streaming pre-spawn (todavía
    // sin spawned()), no lo incluimos en esta lista todavía. Antes esto
    // no chequeaba spawned() mientras que el AddPlayerPacket de al lado
    // sí lo hacía — inconsistencia que podía describirle a este cliente
    // un eid que ese otro jugador nunca terminó de anunciar.
    if (srv_) {
        auto plp = std::make_shared<network::protocol::PlayerListPacket>();
        plp->type = network::protocol::PlayerListPacket::TYPE_ADD;
        for (auto& other : srv_->playersSnapshot()) {
            if (!other) continue;
            if (other.get() != this && !other->spawned()) continue;
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

    // BUG FIX (root cause of the multiplayer crash/kick when a second
    // player joins): the block above only spawns EXISTING players TO
    // this new client. Nothing was ever telling the players who were
    // ALREADY online that THIS player exists. The despawn path (see
    // close(), which broadcasts RemoveEntityPacket to everyone else)
    // has always had this symmetric broadcast — spawn never did.
    //
    // Consequence: already-connected clients start receiving entity
    // packets (MoveEntityPacket, UpdateBlockPacket/LevelEventPacket
    // broadcasts, etc.) that reference an eid they were never told
    // about via AddPlayerPacket/PlayerListPacket. The MCPE 0.15.x
    // client does not tolerate packets for an unknown entity — it
    // corrupts the client's entity state and typically crashes the
    // app outright, which then can't reconnect until the stale
    // RakLib session on the server times out.
    //
    // Fix: mirror the despawn broadcast — announce this player (both
    // the player-list entry and the AddPlayerPacket) to every other
    // already-spawned player, symmetric to close()'s RemoveEntityPacket
    // broadcast.
    if (srv_) {
        auto plp_add = std::make_shared<network::protocol::PlayerListPacket>();
        plp_add->type = network::protocol::PlayerListPacket::TYPE_ADD;
        {
            network::protocol::PlayerListPacket::Entry e;
            parseUuidBytes(cfg_.uuid, e.uuid);
            e.eid    = runtimeId();
            e.name   = getName();
            e.isSlim = "false";
            e.skin   = "";
            plp_add->entries.push_back(std::move(e));
        }

        auto ap_self = std::make_shared<network::protocol::AddPlayerPacket>();
        parseUuidBytes(cfg_.uuid, ap_self->uuid);
        ap_self->username = getName();
        ap_self->eid = runtimeId();
        ap_self->x = (float)pos_.x;
        ap_self->y = (float)pos_.y;
        ap_self->z = (float)pos_.z;
        ap_self->yaw = yaw_;
        ap_self->headYaw = yaw_;
        ap_self->pitch = pitch_;

        for (auto& other : srv_->playersSnapshot()) {
            if (!other || other.get() == this) continue;
            if (!other->spawned()) continue;
            other->sendPacket(plp_add);
            other->sendPacket(ap_self);
        }
    }

    // Broadcast join message. Se dispara PlayerJoinEvent antes: un
    // listener puede reescribir el mensaje, vaciarlo (silencia el
    // broadcast) o cancelar el evento (mismo efecto). La conexión en
    // sí ya está establecida a esta altura — ver el comentario en
    // event/player/PlayerJoinEvent.h.
    if (srv_) {
        auto ev = event::EventManager::get().fire<event::player::PlayerJoinEvent>(
            this, cfg_.username + " joined the game");
        if (!ev.isCancelled() && !ev.message.empty()) {
            srv_->broadcastMessage(ev.message);
        }
    }

    utils::MainLogger::get().info("First spawn for " + cfg_.username);
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
            this, pos_.x, pos_.y, pos_.z, x, y, z);
        if (ev.isCancelled()) {
            // Revertir: reenviar la posicion ANTERIOR al cliente,
            // igual que la rama de anti-cheat de mas arriba.
            auto mp = std::make_shared<network::protocol::MovePlayerPacket>();
            mp->eid = runtimeId();
            mp->x = (float)pos_.x; mp->y = (float)pos_.y; mp->z = (float)pos_.z;
            mp->yaw = yaw_; mp->bodyYaw = yaw_; mp->pitch = pitch_;
            mp->mode = network::protocol::MovePlayerPacket::MODE_RESET;
            sendPacket(mp);
            return;
        }
    }

    pos_.x = x; pos_.y = y; pos_.z = z;
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
        me->x = (float)pos_.x; me->y = (float)pos_.y; me->z = (float)pos_.z;
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
    if (cursestone::redstone::tryInteract(lvl, srv_, x, y, z)) {
        return;
    }

    // PlayerInteractEvent (RIGHT_CLICK_BLOCK) — se dispara ANTES de
    // cualquier rama de logica (flint&steel, extincion de fuego,
    // colocacion de bloque). Un listener puede cancelarlo (p.ej.
    // proteccion de zona) para bloquear toda interaccion con este
    // bloque sin tener que duplicar el chequeo en cada rama de abajo.
    // `item` refleja lo que el cliente reporta tener en mano; ids
    // fuera de rango uint16_t (no deberia pasar, heldItemId es
    // int16_t) se truncan de forma segura via el cast explicito.
    {
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
    if (lvl->getBlockId(x, y, z) == block::CHEST) {
        openChestAt(x, y, z);
        return;
    }

    // Horno — mismo patrón que el cofre: click derecho abre su UI
    // (su propia ventana de contenedor) en vez de colocar un bloque encima.
    if (lvl->getBlockId(x, y, z) == block::FURNACE ||
        lvl->getBlockId(x, y, z) == block::BURNING_FURNACE) {
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
    if (lvl->getBlockId(x, y, z) == block::WORKBENCH &&
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
        if (targetId == FIRE_ID) {
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
                ubp->flags = 0xFF;
                if (srv_) {
                    for (auto& other : srv_->playersSnapshot()) {
                        if (!other) continue;
                        if (!other->isTrackingChunk(tcx, tcz)) continue;
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
        // Flint and steel — delegar toda la lógica a ItemFlintSteel::onActivate().
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
    }
}

void Player::handlePlayerAction(int action, int x, int y, int z, int /*face*/) {
    // PMMP: ACTION_START_BREAK=0, ACTION_ABORT_BREAK=1, ACTION_STOP_BREAK=2.
    // We don't model break progress yet — just log START for diagnostics.
    if (srv_ && srv_->advancedLog() && action == 0) {
        utils::MainLogger::get().info("[ACTION] " + cfg_.username + " started breaking at " +
            std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z));
    }
    // Sprint toggle (protocol 84, Infinity PlayerActionPacket):
    // START_SPRINT=9 / STOP_SPRINT=10 — drives the higher exhaustion
    // rate in tickHungerAndDamage().
    if (action == network::protocol::PlayerActionPacket::ACTION_START_SPRINT) {
        sprinting_ = true;
    } else if (action == network::protocol::PlayerActionPacket::ACTION_STOP_SPRINT) {
        sprinting_ = false;
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
// respawn — se llama cuando el cliente confirma que toco el boton de
// "Respawn" en la pantalla de muerte (ACTION_SPAWN_SAME_DIMENSION).
// No hace nada si el jugador no esta muerto (evita que un cliente que
// mande la accion dos veces, o fuera de orden, reviva salud de mas).
//
// Secuencia: dispara PlayerRespawnEvent (no cancelable — solo puede
// cambiar DONDE reaparece), restaura salud al maximo, teletransporta,
// y sincroniza al cliente con RespawnPacket + SetHealthPacket. No
// reenvia inventario/experiencia todavia porque este proyecto no
// vacia el inventario al morir (no hay drop-on-death implementado),
// asi que no hay nada que restaurar ahi.
// ============================================================
void Player::respawn() {
    if (!isDead()) return;

    math::Vector3 sp = getSpawn();
    auto ev = event::EventManager::get().fire<event::player::PlayerRespawnEvent>(
        this, sp.x, sp.y, sp.z);

    setHealth((int)maxHealth());
    // Respawn always lands at the server spawn, snapped to the surface of
    // the terrain (same rule as first-join: getServerSpawnSafe()), with a
    // fresh food bar (Infinity/Genisys respawn behaviour).
    if (srv_ && srv_->getDefaultLevel()) {
        math::Vector3 safe = getServerSpawnSafe();
        ev.respawnX = safe.x;
        ev.respawnY = safe.y;
        ev.respawnZ = safe.z;
    }
    setFoodLevel(20);
    food_saturation_ = 5.0f;
    pos_.x = ev.respawnX; pos_.y = ev.respawnY; pos_.z = ev.respawnZ;
    // Reset per-life state.
    air_ticks_ = 300;
    fall_distance_ = 0.0f;
    was_on_fire_ = false;
    was_in_lava_ = false;
    starve_ticks_ = 0;
    food_tick_timer_ = 0;
    food_depletion_ = 0;

    if (spawned_.load(std::memory_order_relaxed)) {
        auto rp = std::make_shared<network::protocol::RespawnPacket>();
        rp->x = (float)pos_.x; rp->y = (float)pos_.y; rp->z = (float)pos_.z;
        sendPacket(rp);

        auto sh = std::make_shared<network::protocol::SetHealthPacket>();
        sh->health = (int32_t)health();
        sendPacket(sh);

        // Full attribute re-sync (health + hunger bars back to max).
        sendAllAttributes();

        auto mp = std::make_shared<network::protocol::MovePlayerPacket>();
        mp->eid = runtimeId();
        mp->x = (float)pos_.x; mp->y = (float)pos_.y; mp->z = (float)pos_.z;
        mp->yaw = yaw_; mp->bodyYaw = yaw_; mp->pitch = pitch_;
        mp->mode = network::protocol::MovePlayerPacket::MODE_RESET;
        sendPacket(mp);
    }

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[RESPAWN] " + cfg_.username + " respawned at " +
            std::to_string((int)pos_.x) + "," + std::to_string((int)pos_.y) + "," +
            std::to_string((int)pos_.z));
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
            dropItem((int)pos_.x, (int)pos_.y, (int)pos_.z,
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

    if (srv_->advancedLog()) {
        utils::MainLogger::get().info("[COMBAT] " + cfg_.username + " hit " +
            target->getName() + " for " + std::to_string(BASE_ATTACK_DAMAGE) +
            " (health now " + std::to_string((int)target->health()) + ")" +
            (died ? " — killed" : ""));
    }
}

// ============================================================
// onDeath — Living hook, se llama UNA VEZ cuando health_ cruza a 0
// (ver Living::applyDamage). Dispara PlayerDeathEvent y difunde el
// mensaje de muerte.
//
// No reaparece al jugador aca mismo — eso pasa en Player::respawn(),
// disparado cuando el cliente manda ACTION_SPAWN_SAME_DIMENSION
// (boton "Respawn" en la pantalla de muerte). Hasta que eso ocurra,
// el jugador se queda con isDead()==true y handleMovePlayer/
// handleInteract lo bloquean de seguir jugando.
// ============================================================
void Player::onDeath() {
    std::string msg = cfg_.username + " died";
    auto ev = event::EventManager::get().fire<event::player::PlayerDeathEvent>(this, msg);
    if (!ev.isCancelled() && !ev.message.empty() && srv_) {
        srv_->broadcastMessage(ev.message);
    }
    // Death sequence (protocol 84, mirrors PMMP Player.kill()):
    // EntityEventPacket DEATH_ANIMATION to EVERY spawned player (the dying
    // client first — without it the client never shows the "You died!"
    // screen and cannot click Respawn), then SetHealthPacket(0).
    {
        auto ee = std::make_shared<network::protocol::EntityEventPacket>();
        ee->eid = runtimeId();
        ee->event = network::protocol::EntityEventPacket::DEATH_ANIMATION;
        sendPacket(ee);
        if (srv_) {
            for (auto& other : srv_->playersSnapshot()) {
                if (!other || other.get() == this || !other->spawned()) continue;
                other->sendPacket(ee);
            }
        }

        auto sh = std::make_shared<network::protocol::SetHealthPacket>();
        sh->health = 0;
        sendPacket(sh);

        // Infinity Player::kill(): right after setHealth(0) the server
        // sends the RespawnPacket with the spawn position. The 0.15.10
        // client needs it to arm the death screen's Respawn flow —
        // without it the Respawn button does nothing and the player is
        // stuck on the death screen (can't respawn or leave to menu).
        {
            math::Vector3 sp = getSpawn();
            auto rp = std::make_shared<network::protocol::RespawnPacket>();
            rp->x = (float)sp.x; rp->y = (float)sp.y; rp->z = (float)sp.z;
            sendPacket(rp);
        }
    }
    // Cleanup at death: fire goes out, food resets for the next life.
    was_on_fire_ = false;
    was_in_lava_ = false;
    fall_distance_ = 0.0f;
    food_saturation_ = 5.0f;
    food_depletion_ = 0;
    food_tick_timer_ = 0;
    starve_ticks_ = 0;
    air_ticks_ = 300;
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
// handleDropItem — portado 1:1 de Genisys Player::handleDataPacket
// case DROP_ITEM_PACKET (protocolo 84):
//   1. Verifica que el jugador esté spawned/vivo.
//   2. Confirma que el item esté realmente en el inventario (confianza
//      cero en el paquete).
//   3. PlayerDropItemEvent (cancelable); si se cancela, reenvía el slot.
//   4. Quita el stack del inventario y suelta la entidad item.
// ============================================================
void Player::handleDropItem(network::protocol::DropItemPacket& pk) {
    if (!spawned() || !inv_) return;

    item::ItemStack want{};
    want.id    = (uint16_t)pk.itemId;
    want.meta  = (uint8_t)pk.itemDamage;
    want.count = (int)pk.itemCount;
    if (want.isEmpty()) return;

    if (!inv_->contains(want)) { sendInventory(); return; }
    int slot = inv_->first(want);
    if (slot < 0) { sendInventory(); return; }

    item::ItemStack dropped = inv_->getItem(slot);
    auto ev = event::EventManager::get().fire<event::player::PlayerDropItemEvent>(this, dropped);
    if (ev.isCancelled()) { sendSlot(slot); return; }

    inv_->remove(dropped);
    dropItem((int)std::floor(pos_.x),
             (int)std::floor(pos_.y + 1.3),
             (int)std::floor(pos_.z),
             dropped.id, (uint8_t)dropped.count, dropped.meta);
    // El cliente hace el drop de forma optimista; re-sincronizamos el
    // inventario para que el slot quede consistente server-side.
    sendInventory();
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
        if (other.get() == this || other->isTrackingChunk(cx, cz)) {
            other->sendPacket(pk);
        }
    }
}

// ============================================================
// sendChunkTiles — reenvía los BlockEntityData de todos los tiles del
// chunk a ESTE jugador. PMMP manda los tiles junto con el chunk; sin
// esto, al reconectar el cliente recibe el bloque del cartel pero no su
// NBT, así que el texto no se renderiza hasta que alguien lo re-edite.
// ============================================================
void Player::sendChunkTiles(int32_t cx, int32_t cz) {
    if (!srv_ || !srv_->getDefaultLevel()) return;
    auto tiles = srv_->getDefaultLevel()->getTilesInChunk(cx, cz);
    for (auto& t : tiles) {
        if (!t) continue;
        const auto& p = t->position();
        sendBlockEntityData((int32_t)p.x, (int32_t)p.y, (int32_t)p.z, *t, this);
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
                int32_t pk_cx = (int32_t)di->x >> 4;
                int32_t pk_cz = (int32_t)di->z >> 4;
                for (auto& other : srv_->playersSnapshot()) {
                    if (!other) continue;
                    if (other.get() == this) continue;
                    if (other->isPendingRemoval()) continue;
                    if (!other->spawned()) continue;
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
    int eye_y = (int)std::floor(pos_.y + 1.62f);   // player eye height (0.15)
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
bool Player::isChunkLoaded(int cx, int cz) const {
    int64_t hash = ((int64_t)cx << 32) | ((uint32_t)cz);
    std::lock_guard<std::mutex> lk(chunks_mtx_);
    return used_chunks_.count(hash) > 0;
}
bool Player::isChunkLoading(int cx, int cz) const {
    (void)cx; (void)cz;  // stub — not yet tracking in-flight chunk sends
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
    sendChunkTiles(cx, cz);
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

} // namespace cursestone
