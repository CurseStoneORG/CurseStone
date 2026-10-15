/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
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

#include "world/World.h"

#include "network/protocol/Info.h"
#include "network/protocol/LoginPacket.h"
#include "network/protocol/PlayStatusPacket.h"
#include "network/protocol/StartGamePacket.h"
#include "network/protocol/SetTimePacket.h"
#include "network/protocol/SetSpawnPositionPacket.h"
#include "network/protocol/SetDifficultyPacket.h"
#include "network/protocol/SetHealthPacket.h"
#include "network/protocol/AdventureSettingsPacket.h"
#include "network/protocol/PlayerListPacket.h"
#include "network/protocol/AddPlayerPacket.h"
#include "network/protocol/CraftingDataPacket.h"
#include "network/protocol/RespawnPacket.h"
#include "network/protocol/MovePlayerPacket.h"
#include "network/protocol/EntityEventPacket.h"
#include "network/protocol/SetEntityDataPacket.h"

#include "inventory/CraftingManager.h"
#include "inventory/PlayerInventory.h"

#include "event/EventManager.h"
#include "event/player/PlayerJoinEvent.h"
#include "event/player/PlayerDeathEvent.h"
#include "event/player/PlayerRespawnEvent.h"

#include "permission/BanList.h"

#include "nbt/NBT.h"
#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/ListTag.h"
#include "nbt/tag/Tag.h"

#include "item/ItemNbt.h"

#include "utils/MainLogger.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>

namespace cursestone {

// ============================================================
// Phase 3 — handleLogin: validate the LoginPacket
// ============================================================

// Defined further down next to parseUuidBytes (they belong with the
// PlayerListPacket/AddPlayerPacket helpers); declared here because the
// login path is what fills them in.
static const char* defaultSkinData();
static const char* defaultSkinId();

void Player::handleLogin(network::protocol::LoginPacket& pk) {
    if (logged_in_.load(std::memory_order_relaxed)) {
        return;
    }

    // 1. Protocol version check.
    if (pk.protocol != network::protocol::CURRENT_PROTOCOL) {
        auto resp =
            std::make_shared<network::protocol::PlayStatusPacket>();

        if (pk.protocol < network::protocol::CURRENT_PROTOCOL) {
            resp->status =
                network::protocol::PlayStatusPacket::LOGIN_FAILED_CLIENT;
        } else {
            resp->status =
                network::protocol::PlayStatusPacket::LOGIN_FAILED_SERVER;
        }

        sendPacket(resp);

        close(
            "disconnectionScreen.outdated" +
            std::string(
                pk.protocol < network::protocol::CURRENT_PROTOCOL
                    ? "Client"
                    : "Server"
            )
        );

        return;
    }

    // 2. Pull username/UUID/clientId from the parsed LoginPacket fields.
    cfg_.username  = pk.username;
    cfg_.uuid      = pk.clientUUID;
    cfg_.client_id = std::to_string(pk.clientId);

    // Skin for the PlayerListPacket entries the other players get when
    // this one joins. The 0.15.10 client ALWAYS ships its skin in its
    // login JWT; forward it verbatim (that is exactly what Genisys
    // does), and never let the field go out empty.
    cfg_.skin_id   = pk.skinId;
    cfg_.skin_data.assign(pk.skinData.begin(), pk.skinData.end());

    if (cfg_.skin_data.empty()) {
        cfg_.skin_id   = defaultSkinId();
        cfg_.skin_data = defaultSkinData();

        utils::MainLogger::get().warn(
            cfg_.username +
            " sent no skin in its login packet — using the "
            "default one (an empty skin blob crashes the 0.15.10 client)"
        );
    }

    // 3. Username validation: [a-zA-Z0-9_]{3,16}, disallow "rcon"/"console".
    if (cfg_.username.size() < 3 || cfg_.username.size() > 16) {
        close("disconnectionScreen.invalidName");
        return;
    }

    for (char c : cfg_.username) {
        bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_';

        if (!ok) {
            close("disconnectionScreen.invalidName");
            return;
        }
    }

    std::string lower = cfg_.username;

    // Cast to unsigned char: ::tolower with a negative value (char is
    // signed on x86 Linux, unsigned on ARM/Android) is undefined
    // behaviour and can index outside glibc's ctype table.
    std::transform(
        lower.begin(),
        lower.end(),
        lower.begin(),
        [](unsigned char c) {
            return (char)::tolower(c);
        }
    );

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
    // waiting forever for the server response to its LoginPacket that
    // was simply never coming. RakLib's own session-timeout logic
    // eventually killed the connection from the outside.
    //
    // Fix: a non-standard skin size is not a reason to abort the
    // handshake. Fall back to the safe default skin, then continue
    // through the normal login flow below.
    size_t sl = pk.skinData.size();

    if (sl != 64 * 32 * 4 && sl != 64 * 64 * 4) {
        utils::MainLogger::get().warn(
            "Non-standard skin size (" +
            std::to_string(sl) +
            " bytes) from " +
            cfg_.username +
            " — using default skin"
        );

        pk.skinData.assign(64 * 32 * 4, 0);
    }

    // 5. Server full?
    if (srv_ &&
        srv_->playersSnapshot().size() >=
            (size_t)srv_->maxPlayers()) {
        kick("disconnectionScreen.serverFull");
        return;
    }

    // 6. Hand off to processLogin for whitelist/ban/NBT/spawn.
    // processLogin is folded into handleLogin here because the C++ player
    // owns its own state machine (not a separate async step like PMMP).
    logged_in_.store(true, std::memory_order_relaxed);

    // Whitelist + bans.
    if (!isWhitelisted()) {
        close("Server is white-listed");
        return;
    }

    if (srv_ &&
        (srv_->getNameBans().isBanned(cfg_.username) ||
         srv_->getIPBans().isBanned(cfg_.ip))) {
        close("You are banned");
        return;
    }

    // Duplicate-session kick (same username or UUID).
    if (srv_) {
        for (auto& other : srv_->playersSnapshot()) {
            if (other.get() == this) {
                continue;
            }

            if (!other) {
                continue;
            }

            std::string on = other->getName();

            std::transform(
                on.begin(),
                on.end(),
                on.begin(),
                [](unsigned char c) {
                    return (char)::tolower(c);
                }
            );

            if (on == lower) {
                other->kick("logged in from another location");
            } else if (
                other->loggedIn() &&
                !other->config().uuid.empty() &&
                other->config().uuid == cfg_.uuid
            ) {
                other->kick("logged in from another location");
            }
        }
    }

    // Load NBT (player.dat).
    if (srv_) {
        named_tag_ = srv_->getOfflinePlayerData(cfg_.username);

        if (named_tag_) {
            int32_t gm = mcp_compound_tag_get_int(
                named_tag_.get(),
                "playerGameType",
                srv_->gamemode()
            );

            setGamemode(gm);

            int16_t hp = mcp_compound_tag_get_short(
                named_tag_.get(),
                "Health",
                20
            );

            int16_t mh = mcp_compound_tag_get_short(
                named_tag_.get(),
                "MaxHealth",
                20
            );

            setMaxHealth(mh > 0 ? mh : 20);
            setHealth(hp <= 0 ? 20 : hp);

            int16_t hunger = mcp_compound_tag_get_short(
                named_tag_.get(),
                "Hunger",
                20
            );

            setFoodLevel(hunger);

            // Last known position — prefer PosX/PosY/PosZ (saved on every
            // auto-save and on disconnect). Falls back to the bed spawn
            // if PosX is missing.
            const double sentinel = -99999.999;

            double px = mcp_compound_tag_get_double(
                named_tag_.get(),
                "PosX",
                sentinel
            );

            if (px != sentinel) {
                pos_.x = px;

                pos_.y = mcp_compound_tag_get_double(
                    named_tag_.get(),
                    "PosY",
                    64.0
                );

                pos_.z = mcp_compound_tag_get_double(
                    named_tag_.get(),
                    "PosZ",
                    0.0
                );

                yaw_ = mcp_compound_tag_get_float(
                    named_tag_.get(),
                    "Yaw",
                    0.0f
                );

                pitch_ = mcp_compound_tag_get_float(
                    named_tag_.get(),
                    "Pitch",
                    0.0f
                );
            } else {
                int32_t sx = mcp_compound_tag_get_int(
                    named_tag_.get(),
                    "SpawnX",
                    INT32_MIN
                );

                int32_t sy = mcp_compound_tag_get_int(
                    named_tag_.get(),
                    "SpawnY",
                    INT32_MIN
                );

                int32_t sz = mcp_compound_tag_get_int(
                    named_tag_.get(),
                    "SpawnZ",
                    INT32_MIN
                );

                if (sx != INT32_MIN ||
                    sy != INT32_MIN ||
                    sz != INT32_MIN) {
                    pos_.x = (double)sx + 0.5;
                    pos_.y = (double)sy;
                    pos_.z = (double)sz + 0.5;
                }

                // Otherwise, leave the world-spawn position set in the ctor.
                if (getWorld()) {
                    pos_ = getServerSpawnSafe();
                }
            }

            // Personal spawn point (Infinity Human::saveNBT: SpawnX/Y/Z).
            {
                int32_t sx = mcp_compound_tag_get_int(
                    named_tag_.get(),
                    "SpawnX",
                    INT32_MIN
                );

                int32_t sy = mcp_compound_tag_get_int(
                    named_tag_.get(),
                    "SpawnY",
                    INT32_MIN
                );

                int32_t sz = mcp_compound_tag_get_int(
                    named_tag_.get(),
                    "SpawnZ",
                    INT32_MIN
                );

                if (sx != INT32_MIN &&
                    sy != INT32_MIN &&
                    sz != INT32_MIN) {
                    has_spawn_override_ = true;
                    spawn_x_ = sx;
                    spawn_y_ = sy;
                    spawn_z_ = sz;
                }
            }

            // Restore inventory from the NBT ListTag.
            const mcp_tag_t* inv_tag =
                mcp_compound_tag_get(named_tag_.get(), "Inventory");

            if (inv_tag &&
                mcp_tag_get_type(inv_tag) == MCP_TAG_LIST) {
                const mcp_list_tag_t* inv_list =
                    (const mcp_list_tag_t*)inv_tag;

                int loaded = 0;

                for (size_t i = 0; i < inv_list->count; ++i) {
                    const mcp_tag_t* entry =
                        mcp_list_tag_get(inv_tag, i);

                    if (!entry ||
                        mcp_tag_get_type(entry) != MCP_TAG_COMPOUND) {
                        continue;
                    }

                    int8_t nbt_slot = mcp_compound_tag_get_byte(
                        entry,
                        "Slot",
                        -1
                    );

                    item::ItemStack it;

                    if (!item::itemStackFromNbt(entry, it)) {
                        continue;
                    }

                    int dest_slot = -1;

                    if (nbt_slot >= 0 && nbt_slot < 36) {
                        // Main inventory + hotbar (slots 0..35).
                        dest_slot = nbt_slot;
                    } else if (nbt_slot >= 100 && nbt_slot <= 103) {
                        // Armor: 100=helmet, 101=chest, 102=legs, 103=boots.
                        dest_slot =
                            inventory::PlayerInventory::ARMOR_OFFSET +
                            (nbt_slot - 100);
                    } else if (nbt_slot >= 36 && nbt_slot < 45) {
                        // Legacy CurseStone layout (armor at 36..39).
                        dest_slot = nbt_slot;
                    }

                    // Armor occupies slots 36..39. PLAYER_INV_TOTAL includes
                    // these slots, whereas SIZE excludes them.
                    if (dest_slot >= 0 &&
                        dest_slot < inventory::PLAYER_INV_TOTAL) {
                        inv_->setItem(dest_slot, it);
                        ++loaded;
                    }
                }

                utils::MainLogger::get().info(
                    "Loaded inventory for " +
                    cfg_.username +
                    " (" +
                    std::to_string(loaded) +
                    " items)"
                );
            }

            // Restore held hotbar slot.
            int32_t held = mcp_compound_tag_get_int(
                named_tag_.get(),
                "SelectedItemSlot",
                -1
            );

            if (held >= 0 && held <= 8) {
                inv_->setHeldSlot(held);
            }
        }
    }

    // Tell the client "login OK".
    auto ps =
        std::make_shared<network::protocol::PlayStatusPacket>();

    ps->status = network::protocol::PlayStatusPacket::LOGIN_SUCCESS;
    sendPacket(ps, true);

    // Send StartGamePacket so the client can render the loading screen.
    auto sg =
        std::make_shared<network::protocol::StartGamePacket>();

    if (getWorld()) {
        auto* lvl = getWorld();

        sg->seed      = (int32_t)lvl->seed();
        sg->dimension = lvl->dimension();
        sg->generator = 1;
        sg->gamemode  = cfg_.gamemode & 0x01;
        sg->eid       = static_cast<int64_t>(runtimeId());

        auto sp = lvl->spawn();

        sg->spawnX = (int32_t)sp.x;
        sg->spawnY = (int32_t)sp.y;
        sg->spawnZ = (int32_t)sp.z;

        sg->x = (float)pos_.x;
        sg->y = (float)pos_.y;
        sg->z = (float)pos_.z;

        sg->unknown = lvl->name();
    }

    sendPacket(sg, true);

    // SetTimePacket.
    if (getWorld()) {
        auto st =
            std::make_shared<network::protocol::SetTimePacket>();

        st->time    = getWorld()->time();
        st->started = true;

        sendPacket(st, true);
    }

    // Weather sync.
    if (getWorld()) {
        getWorld()->sendWeatherTo(this);
    }

    // SetSpawnPosition.
    if (getWorld()) {
        auto sp = getWorld()->spawn();

        auto ssp =
            std::make_shared<network::protocol::SetSpawnPositionPacket>();

        ssp->x = (int32_t)sp.x;
        ssp->y = (int32_t)sp.y;
        ssp->z = (int32_t)sp.z;

        sendPacket(ssp, true);
    }

    // SetDifficulty.
    if (srv_) {
        auto sd =
            std::make_shared<network::protocol::SetDifficultyPacket>();

        sd->difficulty = srv_->difficulty();

        sendPacket(sd, true);
    }

    // SetHealth.
    {
        auto sh =
            std::make_shared<network::protocol::SetHealthPacket>();

        sh->health = (int32_t)health();

        sendPacket(sh, true);
    }

    utils::MainLogger::get().info(
        "Player " +
        cfg_.username +
        " logged in (" +
        cfg_.ip +
        ":" +
        std::to_string(cfg_.port) +
        ")"
    );

    // Stream chunks and trigger first spawn.
    sendChunksAroundSpawn();
}

// ============================================================
// Phase 6 — doFirstSpawn
// ============================================================

// Default skin data for clients that did not send one.
static const char* defaultSkinData() {
    static const std::string kData =
        "{\"skinId\":\"Standard_Custom\",\"data\":\""
        "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAZUlEQVR42u3QQREAAAQAMHnFkUkv"
        "cjh7rMCiK+ezECBAgAABAgQIECBAgAABAgQIECBAgAABAgQIECBAgAABAgQIECBAgAABAgQIECBA"
        "gAABAgQIECBAgAABAgQIECBAgAABAgQIECBAgAABAu5bsTTyhgtuRSIAAAAASUVORK5CYII="
        "\",\"geometryName\":\"geometry.humanoid.customGeometry1\"}";

    return kData.c_str();
}

static const char* defaultSkinId() {
    return "Standard_Custom";
}

// Helper: parse UUID string into 16 raw bytes.
static void parseUuidBytes(
    const std::string& s,
    std::array<uint8_t, 16>& out
) {
    out.fill(0);

    if (s.empty()) {
        return;
    }

    size_t bi = 0;

    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }

        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }

        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }

        return -1;
    };

    for (size_t i = 0; i + 1 < s.size() && bi < 16; ++i) {
        char c = s[i];

        if (c == '-') {
            continue;
        }

        int hi = hexVal(c);

        if (hi < 0) {
            continue;
        }

        size_t j = i + 1;

        while (j < s.size() && s[j] == '-') {
            ++j;
        }

        if (j >= s.size()) {
            break;
        }

        int lo = hexVal(s[j]);

        if (lo < 0) {
            continue;
        }

        out[bi++] = (uint8_t)((hi << 4) | lo);
        i = j;
    }
}

void Player::doFirstSpawn() {
    if (spawned_.exchange(true)) {
        return;
    }

    start_time_ms_ = (uint64_t)
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

    last_activity_ms_ = start_time_ms_;

    // AdventureSettings (flight/pvp flags).
    {
        auto as =
            std::make_shared<network::protocol::AdventureSettingsPacket>();

        uint32_t flags = 0;

        if (cfg_.gamemode == 2 || cfg_.gamemode == 3) {
            flags |= 0x01;
        }

        flags |= 0x02;
        flags |= 0x04;
        flags |= 0x08;
        flags |= 0x40;

        if (cfg_.gamemode == 1 || cfg_.gamemode == 3) {
            flags |= 0x80;
        }

        if (cfg_.gamemode == 3) {
            flags |= 0x100;
        }

        as->flags            = flags;
        as->userPermission   = 2;
        as->globalPermission = 2;

        sendPacket(as);
    }

    // CraftingDataPacket.
    if (srv_) {
        auto cd =
            std::make_shared<network::protocol::CraftingDataPacket>();

        if (auto* mgr = srv_->craftingManager()) {
            cd->addAllRecipes(*mgr);

            utils::MainLogger::get().info(
                "Sent " +
                std::to_string(mgr->size()) +
                " recipes to " +
                cfg_.username
            );
        }

        sendPacket(cd);
    }

    // PLAYER_SPAWN.
    {
        auto ps =
            std::make_shared<network::protocol::PlayStatusPacket>();

        ps->status = network::protocol::PlayStatusPacket::PLAYER_SPAWN;

        sendPacket(ps);
    }

    sendCreativeInventory();
    sendAllAttributes();
    sendInventory();

    // Broadcast player-list entries for currently-online players.
    if (srv_) {
        auto plp =
            std::make_shared<network::protocol::PlayerListPacket>();

        plp->type = network::protocol::PlayerListPacket::TYPE_ADD;

        for (auto& other : srv_->playersSnapshot()) {
            if (!other) {
                continue;
            }

            if (other.get() != this && !other->spawned()) {
                continue;
            }

            network::protocol::PlayerListPacket::Entry e;

            parseUuidBytes(other->config().uuid, e.uuid);

            e.eid      = other->runtimeId();
            e.name     = other->getNameTag();
            e.skinId   = other->skinId();
            e.skinData = other->skinData();

            plp->entries.push_back(std::move(e));
        }

        sendPacket(plp);
    }

    // Spawn other online players to this client.
    if (srv_) {
        for (auto& other : srv_->playersSnapshot()) {
            if (other.get() == this) {
                continue;
            }

            if (!other || !other->spawned()) {
                continue;
            }

            if (other->getWorld() != getWorld()) {
                continue;
            }

            auto ap =
                std::make_shared<network::protocol::AddPlayerPacket>();

            parseUuidBytes(other->config().uuid, ap->uuid);

            ap->username = other->getNameTag();
            ap->eid      = other->runtimeId();

            ap->x = (float)other->position().x;
            ap->y = (float)other->position().y;
            ap->z = (float)other->position().z;

            ap->yaw     = other->yaw();
            ap->headYaw = other->yaw();
            ap->pitch   = other->pitch();

            sendPacket(ap);
        }
    }

    announceSelfToOthers();

    // Broadcast join message.
    if (srv_) {
        auto ev =
            event::EventManager::get().fire<event::player::PlayerJoinEvent>(
                this,
                cfg_.username + " joined the game"
            );

        if (!ev.isCancelled() && !ev.message.empty()) {
            srv_->broadcastMessage(ev.message);
        }
    }

    utils::MainLogger::get().info(
        "First spawn for " + cfg_.username
    );
}

// ============================================================
// Visibility helpers — player-list entry + AddPlayerPacket
// ============================================================

void Player::announceSelfToOthers() {
    if (!srv_) {
        return;
    }

    auto plp_add =
        std::make_shared<network::protocol::PlayerListPacket>();

    plp_add->type = network::protocol::PlayerListPacket::TYPE_ADD;

    {
        network::protocol::PlayerListPacket::Entry e;

        parseUuidBytes(cfg_.uuid, e.uuid);

        e.eid      = runtimeId();
        e.name     = getNameTag();
        e.skinId   = skinId();
        e.skinData = skinData();

        plp_add->entries.push_back(std::move(e));
    }

    auto ap_self =
        std::make_shared<network::protocol::AddPlayerPacket>();

    parseUuidBytes(cfg_.uuid, ap_self->uuid);

    ap_self->username = getNameTag();
    ap_self->eid      = runtimeId();

    ap_self->x = (float)pos_.x;
    ap_self->y = (float)pos_.y;
    ap_self->z = (float)pos_.z;

    ap_self->yaw     = yaw_;
    ap_self->headYaw = yaw_;
    ap_self->pitch   = pitch_;

    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this) {
            continue;
        }

        if (other->isPendingRemoval()) {
            continue;
        }

        if (!other->spawned()) {
            continue;
        }

        other->sendPacket(plp_add);

        if (other->getWorld() != getWorld()) {
            continue;
        }

        other->sendPacket(ap_self);
    }
}

void Player::announceTo(Player* viewer) {
    if (!viewer || viewer == this) {
        return;
    }

    if (!viewer->spawned()) {
        return;
    }

    auto plp =
        std::make_shared<network::protocol::PlayerListPacket>();

    plp->type = network::protocol::PlayerListPacket::TYPE_ADD;

    {
        network::protocol::PlayerListPacket::Entry e;

        parseUuidBytes(cfg_.uuid, e.uuid);

        e.eid      = runtimeId();
        e.name     = getName();
        e.skinId   = skinId();
        e.skinData = skinData();

        plp->entries.push_back(std::move(e));
    }

    viewer->sendPacket(plp);

    auto ap =
        std::make_shared<network::protocol::AddPlayerPacket>();

    parseUuidBytes(cfg_.uuid, ap->uuid);

    ap->username = getNameTag();
    ap->eid      = runtimeId();

    ap->x = (float)pos_.x;
    ap->y = (float)pos_.y;
    ap->z = (float)pos_.z;

    ap->yaw     = yaw_;
    ap->headYaw = yaw_;
    ap->pitch   = pitch_;

    viewer->sendPacket(ap);

    // Forward the current pose.
    auto pose =
        std::make_shared<network::protocol::SetEntityDataPacket>();

    pose->eid = runtimeId();

    pose->entries.push_back(
        makePoseFlagEntry(
            sneaking_,
            sprinting_,
            false,
            isOnFire()
        )
    );

    viewer->sendPacket(pose);
}

// ============================================================
// Respawn
// ============================================================

void Player::respawn() {
    if (!isDead()) {
        return;
    }

    math::Vector3 sp = getSpawn();

    auto ev =
        event::EventManager::get().fire<event::player::PlayerRespawnEvent>(
            this,
            sp.x,
            sp.y,
            sp.z
        );

    entity::Living::setHealth((float)maxHealth());

    if (getWorld()) {
        math::Vector3 safe = getServerSpawnSafe();

        ev.respawnX = safe.x;
        ev.respawnY = safe.y;
        ev.respawnZ = safe.z;
    }

    setFoodLevel(20);
    food_saturation_ = 5.0f;

    pos_.x = ev.respawnX;
    pos_.y = ev.respawnY;
    pos_.z = ev.respawnZ;

    // Reset per-life state.
    air_ticks_      = 300;
    fall_distance_  = 0.0f;
    was_on_fire_    = false;
    fire_ticks_     = 0;
    was_in_lava_    = false;
    starve_ticks_   = 0;
    food_tick_timer_ = 0;
    food_depletion_ = 0;

    sneaking_ = false;
    sprinting_ = false;

    if (spawned_.load(std::memory_order_relaxed)) {
        spawn_ack_done_.store(false, std::memory_order_relaxed);

        auto rp =
            std::make_shared<network::protocol::RespawnPacket>();

        rp->x = (float)pos_.x;
        rp->y = (float)pos_.y;
        rp->z = (float)pos_.z;

        sendPacket(rp);

        auto sh =
            std::make_shared<network::protocol::SetHealthPacket>();

        sh->health = (int32_t)health();

        sendPacket(sh);

        sendAllAttributes();

        auto mp =
            std::make_shared<network::protocol::MovePlayerPacket>();

        mp->eid = runtimeId();

        mp->x = (float)pos_.x;
        mp->y = (float)(pos_.y + kEyeHeight);
        mp->z = (float)pos_.z;

        mp->yaw     = yaw_;
        mp->bodyYaw = yaw_;
        mp->pitch   = pitch_;

        mp->mode = network::protocol::MovePlayerPacket::MODE_RESET;

        sendPacket(mp);

        announceSelfToOthers();

        if (srv_) {
            for (auto& other : srv_->playersSnapshot()) {
                if (!other || other.get() == this) {
                    continue;
                }

                if (other->isPendingRemoval()) {
                    continue;
                }

                if (!other->spawned()) {
                    continue;
                }

                if (other->isDead()) {
                    continue;
                }

                other->announceTo(this);
            }
        }

        auto ps =
            std::make_shared<network::protocol::PlayStatusPacket>();

        ps->status = network::protocol::PlayStatusPacket::PLAYER_SPAWN;

        sendPacket(ps);
    }

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info(
            "[RESPAWN] " +
            cfg_.username +
            " respawned at " +
            std::to_string((int)pos_.x) + "," +
            std::to_string((int)pos_.y) + "," +
            std::to_string((int)pos_.z)
        );
    }
}

// ============================================================
// onDeath
// ============================================================

void Player::onDeath() {
    std::string msg = cfg_.username + " died";

    auto ev =
        event::EventManager::get().fire<event::player::PlayerDeathEvent>(
            this,
            msg
        );

    if (!ev.isCancelled() && !ev.message.empty() && srv_) {
        srv_->broadcastMessage(ev.message);
    }

    // Death sequence (protocol 84).
    {
        auto ee =
            std::make_shared<network::protocol::EntityEventPacket>();

        ee->eid   = runtimeId();
        ee->event = network::protocol::EntityEventPacket::DEATH_ANIMATION;

        sendPacket(ee);

        if (srv_) {
            for (auto& other : srv_->playersSnapshot()) {
                if (!other || other.get() == this || !other->spawned()) {
                    continue;
                }

                other->sendPacket(ee);
            }
        }

        auto sh =
            std::make_shared<network::protocol::SetHealthPacket>();

        sh->health = 0;

        sendPacket(sh);

        sendHealthAttribute();

        {
            math::Vector3 sp = getSpawn();

            auto rp =
                std::make_shared<network::protocol::RespawnPacket>();

            rp->x = (float)sp.x;
            rp->y = (float)sp.y;
            rp->z = (float)sp.z;

            sendPacket(rp);
        }
    }

    // Drop inventory items on death in non-creative mode.
    if (inv_ && !isCreative()) {
        for (int i = 0; i < inv_->getSize(); ++i) {
            item::ItemStack it = inv_->getItem(i);

            if (it.isEmpty() || it.id == 0 || it.count <= 0) {
                continue;
            }

            dropItem(
                pos_.x,
                pos_.y,
                pos_.z,
                it.id,
                (uint8_t)it.count,
                it.meta
            );
        }

        // Armor slots.
        for (int i = inv_->getSize(); i < inv_->getSize() + 4; ++i) {
            item::ItemStack it = inv_->getItem(i);

            if (it.isEmpty() || it.id == 0 || it.count <= 0) {
                continue;
            }

            dropItem(
                pos_.x,
                pos_.y,
                pos_.z,
                it.id,
                (uint8_t)it.count,
                it.meta
            );
        }

        inv_->clearAll();
    }

    // Cleanup at death.
    was_on_fire_     = false;
    fire_ticks_      = 0;
    was_in_lava_     = false;
    fall_distance_   = 0.0f;
    food_saturation_ = 5.0f;
    food_depletion_  = 0;
    food_tick_timer_ = 0;
    starve_ticks_    = 0;
    air_ticks_       = 300;
}

void Player::spawnTo(Player* player) {
    if (!player || player == this) {
        return;
    }

    auto ap =
        std::make_shared<network::protocol::AddPlayerPacket>();

    parseUuidBytes(cfg_.uuid, ap->uuid);

    ap->username = cfg_.username;
    ap->eid      = runtimeId();

    ap->x = (float)pos_.x;
    ap->y = (float)pos_.y;
    ap->z = (float)pos_.z;

    ap->yaw     = yaw_;
    ap->headYaw = yaw_;
    ap->pitch   = pitch_;

    player->sendPacket(ap);
}

} // namespace cursestone