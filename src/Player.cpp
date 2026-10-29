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
#include "diagnostics/Diagnostics.h"
#include "world/World.h"
#include "world/format/Chunk.h"
#include "block/BlockIds.h"
#include "network/protocol/DisconnectPacket.h"
#include "network/protocol/MovePlayerPacket.h"
#include "network/protocol/PlayerListPacket.h"
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
#include "utils/CRaii.h"
#include "utils/MainLogger.hpp"
#include "utils/Utils.h"
#include <chrono>
#include <cmath>

namespace cursestone {

// Phase 0: Player construction and destruction.
Player::Player(Server* srv, PlayerConfig cfg)
    : entity::Human(cfg.runtime_eid, nullptr), srv_(srv), cfg_(std::move(cfg)) {
    inv_ = std::make_shared<inventory::PlayerInventory>();
    inv_->setOwner(this); // Enables slot-change packets.
    connected_.store(true, std::memory_order_relaxed);
    setMaxHealth(20);
    setHealth(20);
    // Initialize hunger.
    food_saturation_ = 5.0f;
    food_exhaustion_ = 0.0f;
    last_eat_ms_ = 0;
    last_pos_set_ = false;
    was_in_lava_ = false;
    was_on_fire_ = false;
    // Use world spawn unless login data overrides it.
    if (getWorld()) {
        auto sp = getWorld()->spawn();
        pos_.x = sp.x + 0.5;
        pos_.y = sp.y;
        pos_.z = sp.z + 0.5;
    }
}
Player::~Player() {
    // Never call close() here: shared_from_this() may throw during destruction.
    // close() normally saves data before the object is destroyed.
    // Perform a minimal save if shutdown bypassed close().
    if (connected_.load(std::memory_order_relaxed)) {
        try {
            if (named_tag_ && srv_ && !cfg_.username.empty()) {
                mcp_compound_tag_set_double(named_tag_.get(), "PosX", pos_.x);
                mcp_compound_tag_set_double(named_tag_.get(), "PosY", pos_.y);
                mcp_compound_tag_set_double(named_tag_.get(), "PosZ", pos_.z);
                mcp_compound_tag_set_float(named_tag_.get(), "Yaw", yaw_);
                mcp_compound_tag_set_float(named_tag_.get(), "Pitch", pitch_);
                mcp_compound_tag_set_int(named_tag_.get(), "playerGameType", cfg_.gamemode);
                srv_->saveOfflinePlayerData(cfg_.username, named_tag_);
            }
        } catch (...) {}
        connected_.store(false, std::memory_order_relaxed);
    }
}

bool Player::isOp() const {
    // Read the server's in-memory operator list.
    return srv_ && srv_->isOp(cfg_.username);
}
void Player::setOp(bool v) {
    if (srv_)
        srv_->setOp(cfg_.username, v);
}
bool Player::isBanned() const {
    if (!srv_)
        return false;
    return srv_->getNameBans().isBanned(cfg_.username);
}
void Player::setBanned(bool v) {
    if (!srv_ || cfg_.username.empty())
        return;
    if (v) {
        srv_->getNameBans().add(permission::BanEntry(cfg_.username, "Banned"));
    } else {
        srv_->getNameBans().remove(cfg_.username);
    }
}
bool Player::isWhitelisted() const {
    if (!srv_ || !srv_->whitelistEnabled())
        return true;
    // No whitelist file support yet.
    return true;
}
void Player::setWhitelisted(bool /*v*/) {
}

// uuid string -> the 16 raw bytes the player-list and AddPlayer encoders
// write. Lenient: dashes and non-hex characters are skipped, so both the
// dashed and the packed login UUID forms land on the same 16 bytes.
void Player::parseUuidBytes(const std::string& s, std::array<uint8_t, 16>& out) {
    out.fill(0);
    if (s.empty())
        return;
    size_t bi = 0;
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i + 1 < s.size() && bi < 16; ++i) {
        int hi = hexVal(s[i]);
        if (hi < 0)
            continue; // dashes (and anything non-hex) are skipped
        size_t j = i + 1;
        while (j < s.size() && s[j] == '-')
            ++j;
        if (j >= s.size())
            break;
        int lo = hexVal(s[j]);
        if (lo < 0)
            continue;
        out[bi++] = (uint8_t)((hi << 4) | lo);
        i = j;
    }
}

// Phase 9: Thread-safe player shutdown.
void Player::close(const std::string& reason) {
    // Keep the player alive throughout cleanup.
    // Continue safely if shared_from_this() fails during shutdown.
    std::shared_ptr<Player> self;
    try {
        self = shared_from_this();
    } catch (...) {
        // Continue without the shared ownership safeguard.
    }

    std::call_once(close_once_, [this, &reason]() {
        // Flush queued packets before marking the player disconnected.
        flushPendingPackets();

        bool expected = true;
        if (!connected_.compare_exchange_strong(expected, false)) {
            // Already closed; ensure removal.
            pending_removal_.store(true, std::memory_order_release);
            return;
        }

        utils::MainLogger::get().info("Closing player " + cfg_.username + " (" + cfg_.ip + ":" +
                                      std::to_string(cfg_.port) + "): " + reason);

        // Client-crash diagnostics: a clean quit carries the client's own
        // disconnect packet ("client disconnect"), the quit path uses the
        // default reason and kicks are prefixed. Anything else means the
        // session died from silence (client crash, dead uplink) or a
        // server-side error — preserve the last wire traffic so the
        // client's final moments can be reconstructed. writeReport also
        // prints one console line and drops a report under crashdumps/.
        if (reason != "client disconnect" && reason != "disconnect.quitting" &&
            reason.rfind("kicked: ", 0) != 0) {
            try {
                diagnostics::Diagnostics::get().writeReport(
                    "client-lost",
                    "Player " + cfg_.username + " lost connection (" + reason +
                        ") — no clean disconnect from the client",
                    "recent_packets lists the last packets the server exchanged; "
                    "the last batch sent to this client is what the client "
                    "processed right before it died");
            } catch (...) {
                // Never let diagnostics fail the close path.
            }
        }

        // Release the player's open chest and update its viewer count.
        closeOpenChest();

        // Clear the sleeping state before despawning the player.
        stopSleep();

        // Record session statistics for diagnostics.
        {
            uint64_t now_ms = (uint64_t)(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::system_clock::now().time_since_epoch())
                                             .count());
            uint64_t session_ms = now_ms - (start_time_ms_ > 0 ? start_time_ms_ : now_ms);
            uint64_t last_act = now_ms - (last_activity_ms_ > 0 ? last_activity_ms_ : now_ms);
            size_t chunk_q = 0;
            {
                std::lock_guard<std::mutex> lk(queue_mtx_);
                chunk_q = chunk_queue_.size();
            }
            WorkbenchManager::get().onDisconnect(cfg_.username, reason, session_ms, last_act,
                                                 chunk_q);
        }

        // Return cursor items before saving player data.
        returnFloatingItems();
        try {
            if (named_tag_ && srv_ && !cfg_.username.empty()) {
                saveData();
            }
        } catch (...) {
            utils::MainLogger::get().warn("[CLOSE] " + cfg_.username + " saveData threw exception");
        }

        // Despawn this entity for other players.
        // Use a snapshot to keep recipients alive during packet delivery.
        // Skip players who never received this entity's spawn packet.
        try {
            if (srv_) {
                auto snapshot = srv_->playersSnapshot();
                auto pk = std::make_shared<network::protocol::RemoveEntityPacket>();
                pk->eid = runtimeId();
                for (auto& other : snapshot) {
                    if (!other)
                        continue;
                    if (other.get() == this)
                        continue;
                    if (other->isPendingRemoval())
                        continue;
                    if (!other->spawned())
                        continue;
                    if (other->getWorld() != getWorld())
                        continue; // Same world.
                    try {
                        other->sendPacket(pk);
                    } catch (...) {}
                }
            }
        } catch (...) {}

        // Drop this player from the other clients' player list. The tab
        // entry only disappears when an explicit REMOVE reaches the client
        // — without it the name lingers there until the viewer reconnects.
        // The list is global (doFirstSpawn announces every spawned player),
        // so the removal is broadcast to everyone still listed.
        try {
            if (srv_ && !cfg_.uuid.empty()) {
                auto rm = std::make_shared<network::protocol::PlayerListPacket>();
                rm->type = network::protocol::PlayerListPacket::TYPE_REMOVE;
                network::protocol::PlayerListPacket::Entry re;
                parseUuidBytes(cfg_.uuid, re.uuid);
                rm->entries.push_back(std::move(re));
                for (auto& other : srv_->playersSnapshot()) {
                    if (!other || other.get() == this)
                        continue;
                    if (other->isPendingRemoval() || !other->spawned())
                        continue;
                    try {
                        other->sendPacket(rm);
                    } catch (...) {}
                }
            }
        } catch (...) {}

        // Broadcast the quit message while allowing event cancellation.
        try {
            if (srv_ && !cfg_.username.empty()) {
                auto ev = event::EventManager::get().fire<event::player::PlayerQuitEvent>(
                    this, cfg_.username + " left the game");
                if (!ev.isCancelled() && !ev.message.empty()) {
                    srv_->broadcastMessage(ev.message);
                }
            }
        } catch (...) {}

        // Release loaded chunks and pending chunk requests.
        {
            std::lock_guard<std::mutex> lk(chunks_mtx_);
            used_chunks_.clear();
        }
        {
            std::lock_guard<std::mutex> lk(queue_mtx_);
            chunk_queue_.clear();
        }

        // Do not call closeSession() here; see the shutdown notes above.
        // Mark for removal on the next server tick.
        pending_removal_.store(true, std::memory_order_release);
    });
}

void Player::kick(const std::string& reason) {
    kick(reason, "");
}

void Player::kick(const std::string& reason, const std::string& detail) {
    // Include optional diagnostic details in the disconnect message.
    std::string message = reason;
    if (!detail.empty()) {
        message += "\n";
        message += detail;
    }
    if (connected_.load(std::memory_order_relaxed)) {
        auto dp = std::make_shared<network::protocol::DisconnectPacket>();
        dp->message = message;
        sendPacket(dp);
        // Flush the disconnect packet before closing the session.
        flushPendingPackets();
    }
    // Close the session before close() to avoid re-entrant shutdown crashes.
    if (adv_iface_ && !cfg_.ip.empty()) {
        adv_iface_->closeSession(cfg_.ip, cfg_.port, message);
    }
    // Prefix so Player::close can tell a server-side kick from a client
    // that died in silence (the client-lost report must not fire here).
    close("kicked: " + reason);
}

void Player::teleport(double x, double y, double z) {
    // Allow teleport events to cancel the operation.
    auto ev = event::EventManager::get().fire<event::player::PlayerTeleportEvent>(
        this, pos_.x, pos_.y, pos_.z, x, y, z);
    if (ev.isCancelled())
        return;

    int32_t old_cx = (int)pos_.x >> 4;
    int32_t old_cz = (int)pos_.z >> 4;
    pos_.x = ev.toX;
    pos_.y = ev.toY;
    pos_.z = ev.toZ;
    int32_t new_cx = (int)pos_.x >> 4;
    int32_t new_cz = (int)pos_.z >> 4;

    // Reset chunk streaming when teleporting to another chunk.
    // Previously sent chunks may no longer be loaded by the client.
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

    // Send a position reset packet if the player has spawned.
    if (spawned_.load(std::memory_order_relaxed)) {
        auto pk = std::make_shared<network::protocol::MovePlayerPacket>();
        pk->eid = runtimeId();
        pk->x = (float)pos_.x;
        pk->y = (float)(pos_.y + kEyeHeight); // Eye position.
        pk->z = (float)pos_.z;
        pk->yaw = yaw_;
        pk->bodyYaw = yaw_;
        pk->pitch = pitch_;
        pk->mode = network::protocol::MovePlayerPacket::MODE_RESET;
        pk->onGround = onGround();
        sendPacket(pk);
    }
}
void Player::setGamemode(int gm) {
    int new_gm = gm & 0x03;
    bool was_creative = (cfg_.gamemode & 1) != 0;
    bool is_creative = (new_gm & 1) != 0;
    cfg_.gamemode = new_gm;
    // Cancel fall damage when switching between creative and survival.
    if (was_creative != is_creative)
        fall_distance_ = 0.0f;
    // Refresh the creative catalog when needed.
    if (was_creative != is_creative || new_gm == 3) {
        sendCreativeInventory();
    }
    // Resync inventory to prevent stale client slots after mode changes.
    // Avoid sending inventory packets before the login handshake completes.
    if (spawned_.load(std::memory_order_relaxed)) {
        sendInventory();
    }
}

// Save player NBT and inventory data.
void Player::saveData() {
    if (!srv_ || cfg_.username.empty())
        return;
    if (!named_tag_) {
        // Create an NBT root if none exists.
        named_tag_ = adoptNbt(mcp_compound_tag_new());
    }
    ::mcp_tag* root = named_tag_.get();
    if (!root)
        return;

    // Save position, rotation, health, and hunger.
    mcp_compound_tag_set_double(root, "PosX", pos_.x);
    mcp_compound_tag_set_double(root, "PosY", pos_.y);
    mcp_compound_tag_set_double(root, "PosZ", pos_.z);
    mcp_compound_tag_set_float(root, "Yaw", yaw_);
    mcp_compound_tag_set_float(root, "Pitch", pitch_);
    mcp_compound_tag_set_int(root, "playerGameType", cfg_.gamemode);
    mcp_compound_tag_set_short(root, "Health", (int16_t)health());
    mcp_compound_tag_set_short(root, "MaxHealth", (int16_t)maxHealth());
    mcp_compound_tag_set_short(root, "Hunger", (int16_t)foodLevel());
    mcp_compound_tag_set_float(root, "FoodSaturation", food_saturation_);
    mcp_compound_tag_set_float(root, "FoodExhaustion", food_exhaustion_);
    // Active potion effects (vanilla ActiveEffects list).
    utils::TagGuard fx_list(mcp_list_tag_new_named("ActiveEffects", MCP_TAG_COMPOUND));
    if (fx_list) {
        for (const auto& e : effects()) {
            utils::TagGuard entry(mcp_compound_tag_new_named(""));
            if (!entry)
                continue;
            mcp_compound_tag_set_byte(entry.get(), "Id", (int8_t)e->id());
            mcp_compound_tag_set_byte(entry.get(), "Amplifier", (int8_t)e->amplifier());
            mcp_compound_tag_set_int(entry.get(), "Duration", e->duration());
            if (mcp_list_tag_append(fx_list.get(), entry.get()) == 0)
                entry.release();
        }
    }
    if (mcp_compound_tag_set(root, "ActiveEffects", fx_list.get()) == 0)
        fx_list.release();
    mcp_compound_tag_set_short(root, "Air", (int16_t)air_ticks_);
    // Experience (same keys as the reference Human::saveNBT).
    mcp_compound_tag_set_int(root, "XpLevel", xp_level_);
    mcp_compound_tag_set_float(root, "XpP", xp_progress_);
    mcp_compound_tag_set_long(root, "RuntimeId", runtimeId());
    mcp_compound_tag_set_long(root, "lastPlayed", (int64_t)mcp_utils_time_ms());

    // Save the player's personal spawn point if set.
    if (has_spawn_override_) {
        mcp_compound_tag_set_int(root, "SpawnX", spawn_x_);
        mcp_compound_tag_set_int(root, "SpawnY", spawn_y_);
        mcp_compound_tag_set_int(root, "SpawnZ", spawn_z_);
    }

    // Save inventory and armor in a single list.
    // Include all 40 slots so equipped armor is preserved.
    utils::TagGuard inv_list(mcp_list_tag_new_named("Inventory", MCP_TAG_COMPOUND));
    if (inv_list && inv_) {
        for (int slot = 0; slot < inventory::PLAYER_INV_TOTAL; ++slot) {
            auto it = inv_->getItem(slot);
            if (it.isEmpty())
                continue;
            int nbt_slot = slot;
            if (slot >= inventory::PlayerInventory::ARMOR_OFFSET &&
                slot < inventory::PlayerInventory::ARMOR_OFFSET + 4) {
                nbt_slot = 100 + (slot - inventory::PlayerInventory::ARMOR_OFFSET);
            }
            utils::TagGuard entry(mcp_compound_tag_new_named(""));
            if (!entry)
                continue;
            mcp_compound_tag_set_byte(entry.get(), "Slot", (int8_t)nbt_slot);
            // Serialize item data, including enchantments.
            item::itemStackToNbt(it, entry.get());
            if (mcp_list_tag_append(inv_list.get(), entry.get()) == 0)
                entry.release();
        }
        mcp_compound_tag_set_int(root, "SelectedItemSlot", inv_->heldSlot());
    }
    if (mcp_compound_tag_set(root, "Inventory", inv_list.get()) == 0)
        inv_list.release();

    srv_->saveOfflinePlayerData(cfg_.username, named_tag_);
    if (srv_->debugMode()) {
        utils::MainLogger::get().info("[SAVE] Player " + cfg_.username + " data saved.");
    }
}

// Return the player's personal spawn or the world spawn.
math::Vector3 Player::getSpawn() const {
    // Prefer the last bed spawn when available.
    if (has_spawn_override_) {
        return math::Vector3{(double)spawn_x_, (double)spawn_y_, (double)spawn_z_};
    }
    if (getWorld())
        return getWorld()->spawn();
    return math::Vector3{0, 64, 0};
}

// Find a safe position at the server spawn.
math::Vector3 Player::getServerSpawnSafe() const {
    math::Vector3 sp = getSpawn();
    auto* lvl = getWorld();
    if (!lvl)
        return sp;
    int32_t bx = (int32_t)std::floor(sp.x);
    int32_t bz = (int32_t)std::floor(sp.z);
    auto& chunks = lvl->chunks();
    // Load the spawn chunk if necessary.
    mcp_chunk_t* c = chunks.getChunk(bx >> 4, bz >> 4);
    if (c) {
        for (int y = 127; y > 0; --y) {
            uint8_t id = mcp_chunk_get_block_id(c, bx & 15, y, bz & 15);
            if (id == 0)
                continue; // Air.
            if (id == block::LEAVES || id == block::LEAVES_2)
                continue; // Leaves.
            // Find the first block with space above it.
            uint8_t above = mcp_chunk_get_block_id(c, bx & 15, y + 1, bz & 15);
            if (above == 0 || above == block::WATER || above == block::FLOWING_WATER) {
                sp.y = (double)y + 1.0;
                return sp;
            }
            // Continue searching below obstructed blocks.
        }
    }
    if (sp.y < 1.0)
        sp.y = 64.0; // Default fallback height.
    return sp;
}

} // namespace cursestone