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
#include "Player.h"
#include "player/PlayerInternal.h"
#include "workbench/WorkbenchManager.h"
#include "world/World.h"
#include "world/format/Chunk.h"
#include "block/BlockIds.h"
#include "network/protocol/DisconnectPacket.h"
#include "network/protocol/MovePlayerPacket.h"
#include "network/protocol/RemoveEntityPacket.h"
#include "inventory/PlayerInventory.h"
#include "permission/BanEntry.h"
#include "permission/BanList.h"
#include "event/EventManager.h"
#include "event/player/PlayerQuitEvent.h"
#include "event/player/PlayerTeleportEvent.h"
#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/ListTag.h"
#include "nbt/tag/Tag.h"
#include "item/ItemNbt.h"
#include "utils/MainLogger.hpp"
#include "utils/Utils.h"
#include <chrono>
#include <cmath>

namespace cursestone {

// ============================================================
// Phase 0 — player construction / destruction
// ============================================================
Player::Player(Server* srv, PlayerConfig cfg)
    : entity::Human(cfg.runtime_eid, nullptr), srv_(srv), cfg_(std::move(cfg)) {
    inv_ = std::make_shared<inventory::PlayerInventory>();
    inv_->setOwner(this);  // so onSlotChange can send packets back to us
    connected_.store(true, std::memory_order_relaxed);
    setMaxHealth(20);
    setHealth(20);
    // Hunger state — Infinity player.php: default saturation 5, food 20.
    food_saturation_ = 5.0f;
    food_tick_timer_ = 0;
    food_depletion_ = 0;
    last_pos_set_ = false;
    was_in_lava_ = false;
    was_on_fire_ = false;
    // Position defaults to world spawn; overridden by processLogin if NBT had Pos.
    if (getWorld()) {
        auto sp = getWorld()->spawn();
        pos_.x = sp.x + 0.5;
        pos_.y = sp.y;
        pos_.z = sp.z + 0.5;
    }
}
Player::~Player() {
    // CRITICAL: Do NOT call close() here. close() uses shared_from_this()
    // which throws std::bad_weak_ptr in to destructor (the control block's
    // weak ref is already expired). That exception calls std::terminate()
    // → segfault. This was the root cause of the crash on player disconnect.
    //
    // By the time the destructor runs, close() has already been called
    // (via call_once from the network or main thread) and connected_ is
    // false. If for some reason it wasn't (e.g., server crash during
    // shutdown), we do to minimal inline save here WITHOUT shared_from_this.
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
    // PMMP reads ops.txt; we delegate to server's in-memory op set which is
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
    // not whitelist file yet: treat all as whitelisted.
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
// Calling closeSession() again from within that callback causes to
// re-entrant call into RakLib's C code while its internal state is
// mid-teardown → segfault. This was the root cause of the 100%
// reproducible crash when to player disconnected.
//
// For proactive kicks, player::kick() calls closeSession() itself
// BEFORE close(), so the RakLib layer handles the disconnect.
// ============================================================
void Player::close(const std::string& reason) {
    // Keep this player alive for the entire duration of close(), even
    // if server::tick() concurrently erases its shared_ptr from players_.
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
        // Vuelca any packet pending of the buffer per tick before
        // marcar connected_=false: the player is va, but everything what the
        // server already it envió this tick (p.ej. its own
        // RemoveEntityPacket broadcast) must leaving of truth — after of the
        // flip sendPacket()/flushPendingPackets() already not enviarían nothing.
        flushPendingPackets();

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
        // ContainerClosePacket send is to safe not-op (sendPacket() bails
        // out when !connected_) — what actually matters here is
        // decrementing the TileChest's viewer count and dropping our
        // reference to it, so the chest doesn't think to disconnected
        // player is still looking at it.
        closeOpenChest();

        // Wake the player up if it quit while sleeping: the bed must not
        // stay "occupied" for the next sleeper and the other clients must
        // see the flag byte clear before the RemoveEntityPacket despawns
        // the entity anyway. Infinity effectively does the same via
        // close() → stopSleep() in its player close pipeline.
        stopSleep();

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
        // CRITICAL: iterate to COPY of the snapshot. The snapshot returns
        // shared_ptrs by value, which keeps each player alive for the
        // duration of sendPacket. Without this, to concurrent reap in
        // server::tick() could destroy to player mid-sendPacket.
        //
        // bug FIX (same mechanism that the of MoveEntityPacket in
        // handleMovePlayer/EntityEventPacket in the combate): if `other`
        // still is in its own window of streaming of chunks (without
        // spawned() still), never arrived to receive the AddPlayerPacket of
        // this entity — mandarle igual the RemoveEntityPacket it does
        // referenciar a eid that its client never knew.
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
                    if (other->getWorld() != getWorld()) continue;   // same world
                    try {
                        other->sendPacket(pk);
                    } catch (...) {}
                }
            }
        } catch (...) {}

        // 3. Broadcast quit message. PlayerQuitEvent can reescribir or
        // silenciar the message. Envuelto in try/catch as the rest of
        // close(): a listener with a bug jamás must impedir that the
        // limpieza of the desconexión continúe.
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

        // 6. Flag for removal — server::tick() will erase this player
        //    from players_ on the next tick (main thread).
        pending_removal_.store(true, std::memory_order_release);
    });
}

void Player::kick(const std::string& reason) {
    kick(reason, "");
}

void Player::kick(const std::string& reason, const std::string& detail) {
    // Send to DisconnectPacket first, then close the RakNet session.
    //
    // Nukkit parity: the disconnect screen the client renders carries the
    // full message, so server-side errors that lead to to kick are shown to
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
        // the DisconnectPacket va in the buffer per tick: there is that volcarlo
        // now, before close the sesion RakNet, or the client never it
        // veria (with the envio individual historico salia to the instante).
        flushPendingPackets();
    }
    // Force-close the RakLib session so the client disconnects promptly.
    // This must happen BEFORE close() because close() not longer calls
    // closeSession() (to avoid re-entrant crashes when called from the
    // RakLib timeout path). closeSession() will trigger RakLib to fire
    // onCloseSession() → close(), but call_once() makes that to not-op.
    if (adv_iface_ && !cfg_.ip.empty()) {
        adv_iface_->closeSession(cfg_.ip, cfg_.port, message);
    }
    close(reason);
}

void Player::teleport(double x, double y, double z) {
    // PlayerTeleportEvent — a listener can cancelarlo (p.ej. a
    // command /tp bloqueado by permisos, or a zona that prohibe
    // teletransportarse hacia adentro) for impedir the movement without
    // that the llamador tenga that chequear a valor of retorno: as
    // teleport() not devolvia nothing before this change, ningun place
    // of the code depende of that "always is mueva".
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
    // the streaming loop in onUpdate() only reacts to CROSSING to chunk
    // boundary via its `moved` detection. That detection compares against
    // chunk_queue_'s front entry — after to teleport the queue still held
    // the OLD center, so the very next tick recorded "moved" once, then
    // recentered; but every chunk in the new radius that had already been
    // marked in used_chunks_ by an earlier pass through that area (or by
    // the spawn-ACK re-stream) was SKIPPED as "already sent" even though
    // the client teleported away and may not longer have them loaded.
    // Clearing the per-player sent-set + re-queuing the new spiral makes
    // the destination stream exactly like to fresh spawn: chunks the
    // client still holds arrive as duplicates it ignores, missing ones
    // now render. Only do this when the teleport actually crossed into
    // another chunk — to 2-block hop inside the same chunk needs nothing.
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

    // Optionally broadcast to MovePlayerPacket to confirm position to the client.
    if (spawned_.load(std::memory_order_relaxed)) {
        auto pk = std::make_shared<network::protocol::MovePlayerPacket>();
        pk->eid = runtimeId();
        pk->x   = (float)pos_.x;
        pk->y   = (float)(pos_.y + kEyeHeight);   // wire and is the eye
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
    // A mode switch cancels any fall in progress: distance accumulated
    // before the switch must never land as damage in the other mode.
    if (was_creative != is_creative) fall_distance_ = 0.0f;
    // Re-send creative inventory when creative state changes (PMMP
    // player.php:1461 sends it on every gamemode change).
    if (was_creative != is_creative || new_gm == 3) {
        sendCreativeInventory();
    }
    // bug FIX ("items disappear when switching gamemode"): the block
    // above only refreshes the read-only creative CATALOG (windowid
    // SPECIAL_CREATIVE) — it never touches the player's current
    // inventory (main/hotbar/armor). Every actual item move (dragging
    // from the creative catalog, moving items around) is confirmed
    // one slot at to time via ContainerSetSlotPacket on the same
    // unordered channel that SetPlayerGameTypePacket/
    // AdventureSettingsPacket go out on right after this call (see
    // GamemodeCommand.cpp). If any of those land out of order or the
    // client's UI rebuild (triggered by the mode-switch packet) races
    // ahead of the last slot confirmation, the client is left
    // displaying to stale picture — some slots got their confirmation,
    // others didn't, so to handful of items appear to "vanish" while
    // the rest survive. Forcing to full, authoritative resync of main
    // inventory + armor + hotbar right here — same as what already
    // happens once at doFirstSpawn() — guarantees the client always
    // ends to mode switch showing exactly what the server has, instead
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
// Save this player's NBT + inventory to disk (PMMP player::saveNBT)
// ============================================================
// Mirrors cn.nukkit.player.saveNBT(): the NBT root contains to flat
// "Inventory" ListTag<CompoundTag> with one entry per non-empty slot.
// Each entry is { Slot:byte, id:Short, Count:byte, Damage:Short }.
// Armor uses the same list with Slot in [100..103] (helmet..boots).
// On load we iterate the list and route by Slot range — mirroring
// Nukkit's player.loadNBT() exactly so worlds are interchangeable.
// ============================================================
void Player::saveData() {
    if (!srv_ || cfg_.username.empty()) return;
    if (!named_tag_) {
        // Construct to fresh NBT root if none exists.
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

    // --- Personal spawn point (Infinity Human::saveNBT: SpawnX/Y/Z) ---
    // Only meaningful once a bed was used; read back on login by
    // PlayerLogin.cpp so the bed spawn survives restarts.
    if (has_spawn_override_) {
        mcp_compound_tag_set_int(root, "SpawnX", spawn_x_);
        mcp_compound_tag_set_int(root, "SpawnY", spawn_y_);
        mcp_compound_tag_set_int(root, "SpawnZ", spawn_z_);
    }

    // --- Inventory + Armor as to single ListTag<CompoundTag> ---
    // bug FIX: this loop used to stop at PlayerInventory::SIZE (36),
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
            mcp_compound_tag_set_byte(entry, "Slot", (int8_t)nbt_slot);
            // id / Count / Damage + the "ench" enchantment list, from the
            // shared definition in src/item/ItemNbt.h — the three
            // hand-copied setters that used to sit here never wrote
            // enchantments, which is why to sharpness sword came back
            // unenchanted after every reconnect.
            item::itemStackToNbt(it, entry);
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

// --- Nukkit-compat: getSpawn ---
math::Vector3 Player::getSpawn() const {
    // Personal spawn point (the bed this player last slept in) wins over
    // the world spawn — Infinity Player::getSpawn() reads
    // $this->spawnPosition once setSpawn() has been called.
    if (has_spawn_override_) {
        return math::Vector3{(double)spawn_x_, (double)spawn_y_, (double)spawn_z_};
    }
    if (getWorld()) return getWorld()->spawn();
    return math::Vector3{0, 64, 0};
}

// getServerSpawnSafe — server spawn with and snapped to the actual terrain
// surface of the column (Infinity getSafeSpawn semantics).
//
// FIX ("the spawn is in the sky the first time that entro"): the Normal
// generator's getSpawn() returns Nukkit's static (127.5, 128, 127.5) — to
// and that is only correct by luck. On most seeds the actual surface at that
// column is several blocks lower (or higher), so to first-time player
// started the session floating in the air and fell for seconds.
//
// The fix scans the spawn column top-down for the first block you can
// actually stand on (solid + nothing solid directly above), exactly like
// Infinity's getSafeSpawn: and = highest solid block + 1. If the chunk is
// not loaded (or is all air/void), fall back to the vanilla cloud level.
// X/Z (+0.5 centering) stay exactly as the generator/level.dat defined
// them, so the spawn is deterministic across restarts.
math::Vector3 Player::getServerSpawnSafe() const {
    math::Vector3 sp = getSpawn();
    auto* lvl = getWorld();
    if (!lvl) return sp;
    int32_t bx = (int32_t)std::floor(sp.x);
    int32_t bz = (int32_t)std::floor(sp.z);
    auto& chunks = lvl->chunks();
    // getChunk (not getChunkIfLoaded): at respawn-from-afar the spawn
    // chunk may have been unloaded by the view-distance sweep — generate
    // it synchronously (one chunk, boot-class cost) so the safe-and scan
    // always sees actual terrain instead of falling back to and=64.
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

} // namespace cursestone
