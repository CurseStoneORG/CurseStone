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
#include "item/Item.h"
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
#include "block/BlockIds.h"

#include "item/ItemNbt.h"

#include "utils/MainLogger.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>

namespace cursestone {

// Validate the login packet.
static const char* defaultSkinData();
static const char* defaultSkinId();

void Player::handleLogin(network::protocol::LoginPacket& pk) {
    if (logged_in_.load(std::memory_order_relaxed)) {
        return;
    }

    // Check protocol compatibility.
    if (pk.protocol != network::protocol::CURRENT_PROTOCOL) {
        auto resp = std::make_shared<network::protocol::PlayStatusPacket>();

        if (pk.protocol < network::protocol::CURRENT_PROTOCOL) {
            resp->status = network::protocol::PlayStatusPacket::LOGIN_FAILED_CLIENT;
        } else {
            resp->status = network::protocol::PlayStatusPacket::LOGIN_FAILED_SERVER;
        }

        sendPacket(resp);

        // kick(), not close(): close() leaves the RakNet session open, so an
        // outdated client that answers keepalives keeps a session (and a
        // potential player slot) alive indefinitely.
        kick("disconnectionScreen.outdated" +
             std::string(pk.protocol < network::protocol::CURRENT_PROTOCOL ? "Client" : "Server"));

        return;
    }

    // Store login identity.
    cfg_.username = pk.username;
    cfg_.uuid = pk.clientUUID;
    cfg_.client_id = std::to_string(pk.clientId);

    // Store the client's skin for player-list entries.
    cfg_.skin_id = pk.skinId;
    // skinId is an arbitrary client string echoed to every player via
    // PlayerList/AddPlayer; clamp it so list packets stay well-formed.
    if (cfg_.skin_id.size() > 128)
        cfg_.skin_id.resize(128);
    cfg_.skin_data.assign(pk.skinData.begin(), pk.skinData.end());

    if (cfg_.skin_data.empty()) {
        cfg_.skin_id = defaultSkinId();
        cfg_.skin_data = defaultSkinData();

        utils::MainLogger::get().warn(
            cfg_.username + " sent no skin in its login packet — using the "
                            "default one (an empty skin blob crashes the 0.15.10 client)");
    }

    // Validate username length and allowed characters.
    if (cfg_.username.size() < 3 || cfg_.username.size() > 16) {
        kick("disconnectionScreen.invalidName");
        return;
    }

    for (char c : cfg_.username) {
        bool ok =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';

        if (!ok) {
            kick("disconnectionScreen.invalidName");
            return;
        }
    }

    std::string lower = cfg_.username;

    // Convert safely to lowercase.
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return (char)::tolower(c);
    });

    if (lower == "rcon" || lower == "console") {
        kick("disconnectionScreen.invalidName");
        return;
    }

    // Replace unsupported legacy skin sizes with the default skin.
    size_t sl = pk.skinData.size();

    if (sl != 64 * 32 * 4 && sl != 64 * 64 * 4) {
        utils::MainLogger::get().warn("Non-standard skin size (" + std::to_string(sl) +
                                      " bytes) from " + cfg_.username + " — using default skin");

        pk.skinData.assign(64 * 32 * 4, 0);
    }

    // Reject connections when the server is full.
    if (srv_ && srv_->playersSnapshot().size() >= (size_t)srv_->maxPlayers()) {
        kick("disconnectionScreen.serverFull");
        return;
    }

    // Continue with whitelist, bans, NBT, and spawning.
    logged_in_.store(true, std::memory_order_relaxed);

    // Check whitelist and bans.
    if (!isWhitelisted()) {
        kick("Server is white-listed");
        return;
    }

    if (srv_ &&
        (srv_->getNameBans().isBanned(cfg_.username) || srv_->getIPBans().isBanned(cfg_.ip))) {
        kick("You are banned");
        return;
    }

    // Kick duplicate sessions by username or UUID.
    if (srv_) {
        for (auto& other : srv_->playersSnapshot()) {
            if (other.get() == this) {
                continue;
            }

            if (!other) {
                continue;
            }

            std::string on = other->getName();

            std::transform(on.begin(), on.end(), on.begin(), [](unsigned char c) {
                return (char)::tolower(c);
            });

            if (on == lower) {
                other->kick("logged in from another location");
            } else if (other->loggedIn() && !other->config().uuid.empty() &&
                       other->config().uuid == cfg_.uuid) {
                other->kick("logged in from another location");
            }
        }
    }

    // Load player data from NBT.
    if (srv_) {
        named_tag_ = srv_->getOfflinePlayerData(cfg_.username);

        if (named_tag_) {
            int32_t gm =
                mcp_compound_tag_get_int(named_tag_.get(), "playerGameType", srv_->gamemode());

            setGamemode(gm);

            int16_t hp = mcp_compound_tag_get_short(named_tag_.get(), "Health", 20);

            int16_t mh = mcp_compound_tag_get_short(named_tag_.get(), "MaxHealth", 20);

            setMaxHealth(mh > 0 ? mh : 20);
            setHealth(hp <= 0 ? 20 : hp);

            int16_t hunger = mcp_compound_tag_get_short(named_tag_.get(), "Hunger", 20);

            setFoodLevel(hunger);
            // Restore hunger saturation and exhaustion.
            food_saturation_ = mcp_compound_tag_get_float(named_tag_.get(), "FoodSaturation", 5.0f);
            food_exhaustion_ = mcp_compound_tag_get_float(named_tag_.get(), "FoodExhaustion", 0.0f);

            // Restore underwater air and the saved XP bar state.
            air_ticks_ = mcp_compound_tag_get_short(named_tag_.get(), "Air", (int16_t)300);
            if (air_ticks_ < 0 || air_ticks_ > 300)
                air_ticks_ = 300;
            xp_level_ = mcp_compound_tag_get_int(named_tag_.get(), "XpLevel", 0);
            if (xp_level_ < 0)
                xp_level_ = 0;
            xp_progress_ = mcp_compound_tag_get_float(named_tag_.get(), "XpP", 0.0f);
            if (xp_progress_ < 0.0f || xp_progress_ > 1.0f)
                xp_progress_ = 0.0f;
            (void)0;

            // Restore persisted potion effects (no packets yet — the client
            // gets them at first spawn via sendActiveEffects()).
            const mcp_tag_t* fx_tag = mcp_compound_tag_get(named_tag_.get(), "ActiveEffects");
            if (fx_tag && mcp_tag_get_type(fx_tag) == MCP_TAG_LIST) {
                for (size_t i = 0, n = mcp_list_tag_size(fx_tag); i < n; ++i) {
                    const mcp_tag_t* entry = mcp_list_tag_get(fx_tag, i);
                    if (!entry || mcp_tag_get_type(entry) != MCP_TAG_COMPOUND)
                        continue;
                    int8_t id = mcp_compound_tag_get_byte(entry, "Id", 0);
                    int8_t amp = mcp_compound_tag_get_byte(entry, "Amplifier", 0);
                    int32_t dur = mcp_compound_tag_get_int(entry, "Duration", 0);
                    if (id <= 0 || dur <= 0)
                        continue;
                    addEffect(std::make_shared<entity::Effect>(
                        entity::Effect::create((uint8_t)id, dur, amp)));
                }
            }

            // Restore the last position or use the saved spawn.
            const double sentinel = -99999.999;

            double px = mcp_compound_tag_get_double(named_tag_.get(), "PosX", sentinel);

            if (px != sentinel) {
                pos_.x = px;

                pos_.y = mcp_compound_tag_get_double(named_tag_.get(), "PosY", 64.0);

                pos_.z = mcp_compound_tag_get_double(named_tag_.get(), "PosZ", 0.0);

                yaw_ = mcp_compound_tag_get_float(named_tag_.get(), "Yaw", 0.0f);

                pitch_ = mcp_compound_tag_get_float(named_tag_.get(), "Pitch", 0.0f);
            } else {
                int32_t sx = mcp_compound_tag_get_int(named_tag_.get(), "SpawnX", INT32_MIN);

                int32_t sy = mcp_compound_tag_get_int(named_tag_.get(), "SpawnY", INT32_MIN);

                int32_t sz = mcp_compound_tag_get_int(named_tag_.get(), "SpawnZ", INT32_MIN);

                if (sx != INT32_MIN || sy != INT32_MIN || sz != INT32_MIN) {
                    pos_.x = (double)sx + 0.5;
                    pos_.y = (double)sy;
                    pos_.z = (double)sz + 0.5;
                }

                // Use the safe world spawn when no position is saved.
                if (getWorld()) {
                    pos_ = getServerSpawnSafe();
                }
            }

            // Restore the personal spawn point.
            {
                int32_t sx = mcp_compound_tag_get_int(named_tag_.get(), "SpawnX", INT32_MIN);

                int32_t sy = mcp_compound_tag_get_int(named_tag_.get(), "SpawnY", INT32_MIN);

                int32_t sz = mcp_compound_tag_get_int(named_tag_.get(), "SpawnZ", INT32_MIN);

                if (sx != INT32_MIN && sy != INT32_MIN && sz != INT32_MIN) {
                    has_spawn_override_ = true;
                    spawn_x_ = sx;
                    spawn_y_ = sy;
                    spawn_z_ = sz;
                }
            }

            // Restore inventory items from NBT.
            const mcp_tag_t* inv_tag = mcp_compound_tag_get(named_tag_.get(), "Inventory");

            if (inv_tag && mcp_tag_get_type(inv_tag) == MCP_TAG_LIST) {
                int loaded = 0;

                for (size_t i = 0, n = mcp_list_tag_size(inv_tag); i < n; ++i) {
                    const mcp_tag_t* entry = mcp_list_tag_get(inv_tag, i);

                    if (!entry || mcp_tag_get_type(entry) != MCP_TAG_COMPOUND) {
                        continue;
                    }

                    int8_t nbt_slot = mcp_compound_tag_get_byte(entry, "Slot", -1);

                    item::ItemStack it;

                    if (!item::itemStackFromNbt(entry, it)) {
                        continue;
                    }

                    int dest_slot = -1;

                    if (nbt_slot >= 0 && nbt_slot < 36) {
                        // Main inventory and hotbar slots.
                        dest_slot = nbt_slot;
                    } else if (nbt_slot >= 100 && nbt_slot <= 103) {
                        // Map legacy armor slots to inventory slots.
                        dest_slot = inventory::PlayerInventory::ARMOR_OFFSET + (nbt_slot - 100);
                    } else if (nbt_slot >= 36 && nbt_slot < 45) {
                        // Support legacy CurseStone inventory slots.
                        dest_slot = nbt_slot;
                    }

                    // Validate the destination inventory slot.
                    if (dest_slot >= 0 && dest_slot < inventory::PLAYER_INV_TOTAL) {
                        // Clamp legacy stacks to the item's own max stack
                        // size. A player file written before the max-stack
                        // fix can carry a count=2 bow — feeding it into the
                        // inventory unclamped would re-seed the illegal
                        // stack that broke held-item rendering and shooting.
                        {
                            const item::Item* def = item::Item::get(it.id);
                            int itemMax = def ? def->maxStackSize() : 64;
                            if (itemMax < 1)
                                itemMax = 1;
                            if (it.count > itemMax)
                                it.count = itemMax;
                        }
                        inv_->setItem(dest_slot, it);
                        ++loaded;
                    }
                }

                utils::MainLogger::get().info("Loaded inventory for " + cfg_.username + " (" +
                                              std::to_string(loaded) + " items)");
            }

            // Restore the selected hotbar slot.
            int32_t held = mcp_compound_tag_get_int(named_tag_.get(), "SelectedItemSlot", -1);

            if (held >= 0 && held <= 8) {
                inv_->setHeldSlot(held);
            }
        }
    }

    // Confirm successful login.
    auto ps = std::make_shared<network::protocol::PlayStatusPacket>();

    ps->status = network::protocol::PlayStatusPacket::LOGIN_SUCCESS;
    sendPacket(ps, true);

    // Initialize the game session.
    auto sg = std::make_shared<network::protocol::StartGamePacket>();

    if (getWorld()) {
        auto* lvl = getWorld();

        sg->seed = (int32_t)lvl->seed();
        sg->dimension = lvl->dimension();
        sg->generator = 1;
        sg->gamemode = cfg_.gamemode & 0x01;
        sg->eid = static_cast<int64_t>(runtimeId());

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

    // Synchronize world time.
    if (getWorld()) {
        auto st = std::make_shared<network::protocol::SetTimePacket>();

        st->time = getWorld()->time();
        st->started = true;

        sendPacket(st, true);
    }

    // Synchronize weather.
    if (getWorld()) {
        getWorld()->sendWeatherTo(this);
    }

    // Send the world spawn position.
    if (getWorld()) {
        auto sp = getWorld()->spawn();

        auto ssp = std::make_shared<network::protocol::SetSpawnPositionPacket>();

        ssp->x = (int32_t)sp.x;
        ssp->y = (int32_t)sp.y;
        ssp->z = (int32_t)sp.z;

        sendPacket(ssp, true);
    }

    // Synchronize server difficulty.
    if (srv_) {
        auto sd = std::make_shared<network::protocol::SetDifficultyPacket>();

        sd->difficulty = srv_->difficulty();

        sendPacket(sd, true);
    }

    // Synchronize player health.
    {
        auto sh = std::make_shared<network::protocol::SetHealthPacket>();

        sh->health = (int32_t)health();

        sendPacket(sh, true);
    }

    utils::MainLogger::get().info("Player " + cfg_.username + " logged in (" + cfg_.ip + ":" +
                                  std::to_string(cfg_.port) + ")");

    // Load chunks around the spawn point.
    sendChunksAroundSpawn();
}

// Initialize the first player spawn.
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

void Player::doFirstSpawn() {
    if (spawned_.exchange(true)) {
        return;
    }

    start_time_ms_ = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    last_activity_ms_ = start_time_ms_;

    // Configure adventure settings and permissions.
    {
        auto as = std::make_shared<network::protocol::AdventureSettingsPacket>();

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

        as->flags = flags;
        as->userPermission = 2;
        as->globalPermission = 2;

        sendPacket(as);
    }

    // Send available crafting recipes.
    if (srv_) {
        auto cd = std::make_shared<network::protocol::CraftingDataPacket>();

        if (auto* mgr = srv_->craftingManager()) {
            cd->addAllRecipes(*mgr);

            utils::MainLogger::get().info("Sent " + std::to_string(mgr->size()) + " recipes to " +
                                          cfg_.username);
        }

        sendPacket(cd);
    }

    // Notify the client that the player spawned.
    {
        auto ps = std::make_shared<network::protocol::PlayStatusPacket>();

        ps->status = network::protocol::PlayStatusPacket::PLAYER_SPAWN;

        sendPacket(ps);
    }

    sendCreativeInventory();
    sendAllAttributes();
    // Reference parity: after login the client receives a self metadata
    // packet clearing the leash holder (eid 0, DATA_LEAD_HOLDER = -1).
    {
        auto lead = std::make_shared<network::protocol::SetEntityDataPacket>();
        lead->eid = 0;
        network::protocol::SetEntityDataPacket::Entry lh;
        lh.index = network::protocol::SetEntityDataPacket::PROP_LEAD_HOLDER;
        lh.type = network::protocol::SetEntityDataPacket::TYPE_LONG;
        lh.ival = -1;
        lead->entries.push_back(lh);
        sendPacket(lead);
    }
    // Re-sync effects restored from the .dat so the client shows them.
    sendActiveEffects();
    sendInventory();

    // Send player-list entries for online players.
    if (srv_) {
        auto plp = std::make_shared<network::protocol::PlayerListPacket>();

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

            e.eid = other->runtimeId();
            e.name = other->getNameTag();
            e.skinId = other->skinId();
            e.skinData = other->skinData();

            plp->entries.push_back(std::move(e));
        }

        sendPacket(plp);
    }

    // Spawn other online players for this client.
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

            auto ap = std::make_shared<network::protocol::AddPlayerPacket>();

            parseUuidBytes(other->config().uuid, ap->uuid);

            ap->username = other->getNameTag();
            ap->eid = other->runtimeId();

            ap->x = (float)other->position().x;
            ap->y = (float)other->position().y;
            ap->z = (float)other->position().z;

            ap->yaw = other->yaw();
            ap->headYaw = other->yaw();
            ap->pitch = other->pitch();

            // Carry the other player's held item so this client renders it
            // from the first spawn packet.
            if (auto oinv = other->getInventory()) {
                auto oheld = oinv->getHeldItem();
                ap->itemId = (int16_t)oheld.id;
                ap->itemCount = oheld.count;
                ap->itemDamage = (int16_t)oheld.meta;
                ap->itemNbt = oheld.nbt;
            }

            sendPacket(ap);

            // Full spawn metadata from the other player (flags, air,
            // nametag, leash, tint, sleep) — the reference spawn packet
            // carries the complete dataProperties.
            other->sendFullEntityData(this);
        }
    }

    announceSelfToOthers();

    // Broadcast the join event message.
    if (srv_) {
        auto ev = event::EventManager::get().fire<event::player::PlayerJoinEvent>(
            this, cfg_.username + " joined the game");

        if (!ev.isCancelled() && !ev.message.empty()) {
            srv_->broadcastMessage(ev.message);
        }
    }

    utils::MainLogger::get().info("First spawn for " + cfg_.username);
}

// Manage player visibility and spawn packets.
void Player::announceSelfToOthers() {
    if (!srv_) {
        return;
    }

    auto plp_add = std::make_shared<network::protocol::PlayerListPacket>();

    plp_add->type = network::protocol::PlayerListPacket::TYPE_ADD;

    {
        network::protocol::PlayerListPacket::Entry e;

        parseUuidBytes(cfg_.uuid, e.uuid);

        e.eid = runtimeId();
        e.name = getNameTag();
        e.skinId = skinId();
        e.skinData = skinData();

        plp_add->entries.push_back(std::move(e));
    }

    auto ap_self = std::make_shared<network::protocol::AddPlayerPacket>();

    parseUuidBytes(cfg_.uuid, ap_self->uuid);

    ap_self->username = getNameTag();
    ap_self->eid = runtimeId();

    ap_self->x = (float)pos_.x;
    ap_self->y = (float)pos_.y;
    ap_self->z = (float)pos_.z;

    ap_self->yaw = yaw_;
    ap_self->headYaw = yaw_;
    ap_self->pitch = pitch_;

    // Carry the held item so existing players render it on first view.
    if (inv_) {
        auto held = inv_->getHeldItem();
        ap_self->itemId = (int16_t)held.id;
        ap_self->itemCount = held.count;
        ap_self->itemDamage = (int16_t)held.meta;
        ap_self->itemNbt = held.nbt;
    }

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

        // Existing players also get the full metadata, exactly like the
        // reference spawn packet carries the complete dataProperties.
        sendFullEntityData(other.get());
    }
}

// Build and send the complete spawn metadata for this player to one viewer.
// Mirrors the reference spawn dataProperties: pose flags, air, nametag,
// show-nametag, silent, no-ai, leash holder, potion tint, and the sleeping
// state — so a viewer that spawns an already-affected player renders it
// exactly like players that were present when the state changed.
void Player::sendFullEntityData(Player* viewer) {
    if (!viewer || viewer == this)
        return;

    auto pose = std::make_shared<network::protocol::SetEntityDataPacket>();
    pose->eid = runtimeId();

    pose->entries.push_back(
        makePoseFlagEntry(sneaking_, sprinting_, bow_pose_active_, isOnFire(), invisible_));

    network::protocol::SetEntityDataPacket::Entry air;
    air.index = network::protocol::SetEntityDataPacket::PROP_AIR;
    air.type = network::protocol::SetEntityDataPacket::TYPE_SHORT;
    air.ival = air_ticks_;
    pose->entries.push_back(air);

    network::protocol::SetEntityDataPacket::Entry nametag;
    nametag.index = network::protocol::SetEntityDataPacket::PROP_NAMETAG;
    nametag.type = network::protocol::SetEntityDataPacket::TYPE_STRING;
    nametag.sval = getNameTag();
    pose->entries.push_back(nametag);

    network::protocol::SetEntityDataPacket::Entry showTag;
    showTag.index = network::protocol::SetEntityDataPacket::PROP_SHOW_NAMETAG;
    showTag.type = network::protocol::SetEntityDataPacket::TYPE_BYTE;
    showTag.ival = 1;
    pose->entries.push_back(showTag);

    network::protocol::SetEntityDataPacket::Entry silent;
    silent.index = network::protocol::SetEntityDataPacket::PROP_SILENT;
    silent.type = network::protocol::SetEntityDataPacket::TYPE_BYTE;
    silent.ival = 0;
    pose->entries.push_back(silent);

    network::protocol::SetEntityDataPacket::Entry noAi;
    noAi.index = network::protocol::SetEntityDataPacket::PROP_NO_AI;
    noAi.type = network::protocol::SetEntityDataPacket::TYPE_BYTE;
    noAi.ival = 0;
    pose->entries.push_back(noAi);

    network::protocol::SetEntityDataPacket::Entry leadHolder;
    leadHolder.index = network::protocol::SetEntityDataPacket::PROP_LEAD_HOLDER;
    leadHolder.type = network::protocol::SetEntityDataPacket::TYPE_LONG;
    leadHolder.ival = -1;
    pose->entries.push_back(leadHolder);

    network::protocol::SetEntityDataPacket::Entry lead;
    lead.index = network::protocol::SetEntityDataPacket::PROP_LEAD;
    lead.type = network::protocol::SetEntityDataPacket::TYPE_BYTE;
    lead.ival = 0;
    pose->entries.push_back(lead);

    if (uint32_t color = blendedEffectColor()) {
        network::protocol::SetEntityDataPacket::Entry col;
        col.index = network::protocol::SetEntityDataPacket::PROP_POTION_COLOR;
        col.type = network::protocol::SetEntityDataPacket::TYPE_INT;
        col.ival = (int32_t)color;
        pose->entries.push_back(col);

        network::protocol::SetEntityDataPacket::Entry amb;
        amb.index = network::protocol::SetEntityDataPacket::PROP_POTION_AMBIENT;
        amb.type = network::protocol::SetEntityDataPacket::TYPE_BYTE;
        amb.ival = 0;
        pose->entries.push_back(amb);
    }

    if (sleeping_) {
        network::protocol::SetEntityDataPacket::Entry bedPos;
        bedPos.index = network::protocol::SetEntityDataPacket::PROP_PLAYER_BED_POSITION;
        bedPos.type = network::protocol::SetEntityDataPacket::TYPE_POS;
        bedPos.px = sleep_x_;
        bedPos.py = sleep_y_;
        bedPos.pz = sleep_z_;
        pose->entries.push_back(bedPos);

        network::protocol::SetEntityDataPacket::Entry flags;
        flags.index = network::protocol::SetEntityDataPacket::PROP_PLAYER_FLAGS;
        flags.type = network::protocol::SetEntityDataPacket::TYPE_BYTE;
        flags.ival = network::protocol::SetEntityDataPacket::PLAYER_FLAG_SLEEP;
        pose->entries.push_back(flags);
    }

    viewer->sendPacket(pose);
}

void Player::announceTo(Player* viewer) {
    if (!viewer || viewer == this) {
        return;
    }

    if (!viewer->spawned()) {
        return;
    }

    auto plp = std::make_shared<network::protocol::PlayerListPacket>();

    plp->type = network::protocol::PlayerListPacket::TYPE_ADD;

    {
        network::protocol::PlayerListPacket::Entry e;

        parseUuidBytes(cfg_.uuid, e.uuid);

        e.eid = runtimeId();
        e.name = getName();
        e.skinId = skinId();
        e.skinData = skinData();

        plp->entries.push_back(std::move(e));
    }

    viewer->sendPacket(plp);

    auto ap = std::make_shared<network::protocol::AddPlayerPacket>();

    parseUuidBytes(cfg_.uuid, ap->uuid);

    ap->username = getNameTag();
    ap->eid = runtimeId();

    ap->x = (float)pos_.x;
    ap->y = (float)pos_.y;
    ap->z = (float)pos_.z;

    ap->yaw = yaw_;
    ap->headYaw = yaw_;
    ap->pitch = pitch_;

    // Carry the held item so the viewer renders it on first sight.
    if (inv_) {
        auto held = inv_->getHeldItem();
        ap->itemId = (int16_t)held.id;
        ap->itemCount = held.count;
        ap->itemDamage = (int16_t)held.meta;
        ap->itemNbt = held.nbt;
    }

    viewer->sendPacket(ap);

    // Send the player's complete spawn metadata (flags, air, tint, sleep).
    sendFullEntityData(viewer);
}

// Respawn a dead player.
void Player::respawn() {
    if (!isDead()) {
        return;
    }

    // A saved bed spawn only survives while the bed still exists: the
    // stored point is the bed head, so anything that is not a bed block
    // there (broken, replaced) falls back to the world spawn.
    if (has_spawn_override_) {
        if ((srv_ && !srv_->bedSpawnEnabled()) ||
            !getWorld() ||
            getWorld()->getBlockId(spawn_x_, spawn_y_, spawn_z_) != block::BED_BLOCK) {
            resetSpawnToDefault();
        }
    }

    math::Vector3 sp = getSpawn();

    auto ev =
        event::EventManager::get().fire<event::player::PlayerRespawnEvent>(this, sp.x, sp.y, sp.z);

    entity::Living::setHealth((float)maxHealth());

    if (getWorld()) {
        math::Vector3 safe = getServerSpawnSafe();

        ev.respawnX = safe.x;
        ev.respawnY = safe.y;
        ev.respawnZ = safe.z;
    }

    setFoodLevel(20);
    food_saturation_ = 5.0f;
    food_exhaustion_ = 0.0f;
    last_eat_ms_ = 0;

    pos_.x = ev.respawnX;
    pos_.y = ev.respawnY;
    pos_.z = ev.respawnZ;

    // Reset state for the new life.
    air_ticks_ = 300;
    fall_distance_ = 0.0f;
    was_on_fire_ = false;
    fire_ticks_ = 0;
    was_in_lava_ = false;
    starve_ticks_ = 0;

    sneaking_ = false;
    sprinting_ = false;

    // Defensive pose reset for the new life (a draw in progress when the
    // previous life ended must not leak into this one).
    bow_start_action_ = -1;
    if (bow_pose_active_)
        broadcastBowPose(false);

    if (spawned_.load(std::memory_order_relaxed)) {
        spawn_ack_done_.store(false, std::memory_order_relaxed);

        auto rp = std::make_shared<network::protocol::RespawnPacket>();

        rp->x = (float)pos_.x;
        rp->y = (float)pos_.y;
        rp->z = (float)pos_.z;

        sendPacket(rp);

        auto sh = std::make_shared<network::protocol::SetHealthPacket>();

        sh->health = (int32_t)health();

        sendPacket(sh);

        sendAllAttributes();

        // Authoritative inventory resync: the client clears its local
        // inventory on death, so keep-inventory runs need the push to
        // see their items again (and drop deaths confirm the empty state).
        sendInventory();

        auto mp = std::make_shared<network::protocol::MovePlayerPacket>();

        mp->eid = runtimeId();

        mp->x = (float)pos_.x;
        mp->y = (float)(pos_.y + kEyeHeight);
        mp->z = (float)pos_.z;

        mp->yaw = yaw_;
        mp->bodyYaw = yaw_;
        mp->pitch = pitch_;

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

        auto ps = std::make_shared<network::protocol::PlayStatusPacket>();

        ps->status = network::protocol::PlayStatusPacket::PLAYER_SPAWN;

        sendPacket(ps);
    }

    if (srv_ && srv_->debugMode()) {
        utils::MainLogger::get().info(
            "[RESPAWN] " + cfg_.username + " respawned at " + std::to_string((int)pos_.x) + "," +
            std::to_string((int)pos_.y) + "," + std::to_string((int)pos_.z));
    }
}

// Handle player death.
void Player::onDeath() {
    // Vanilla clears potion effects on death (before the death flow so
    // the removal packets reach a still-connected client).
    removeAllEffects();

    std::string msg = cfg_.username + " died";

    auto ev = event::EventManager::get().fire<event::player::PlayerDeathEvent>(this, msg);

    if (!ev.isCancelled() && !ev.message.empty() && srv_) {
        srv_->broadcastMessage(ev.message);
    }

    // Send the death animation and health update.
    {
        auto ee = std::make_shared<network::protocol::EntityEventPacket>();

        ee->eid = runtimeId();
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

        auto sh = std::make_shared<network::protocol::SetHealthPacket>();

        sh->health = 0;

        sendPacket(sh);

        sendHealthAttribute();

        // Send the respawn position.
        {
            math::Vector3 sp = getSpawn();

            auto rp = std::make_shared<network::protocol::RespawnPacket>();

            rp->x = (float)sp.x;
            rp->y = (float)sp.y;
            rp->z = (float)sp.z;

            sendPacket(rp);
        }
    }

    // Drop inventory items outside creative mode unless keep-inventory is on.
    if (inv_ && !isCreative()) {
        returnFloatingItems();
        if (srv_ && srv_->keepInventory()) {
            // Cursor items were returned above; the whole inventory stays.
        } else {
            for (int i = 0; i < inv_->getSize(); ++i) {
                item::ItemStack it = inv_->getItem(i);

                if (it.isEmpty() || it.id == 0 || it.count <= 0) {
                    continue;
                }

                // Split oversized stacks per the item's max stack size and
                // carry the stack's NBT (custom names) into the drop — the
                // missing nbt argument here is what made a named item hit
                // the ground nameless after death.
                const item::Item* dropDef = item::Item::get(it.id);
                int itemMax = dropDef ? dropDef->maxStackSize() : 64;
                if (itemMax < 1)
                    itemMax = 1;
                int remaining = it.count;
                while (remaining > 0) {
                    int chunk = remaining > itemMax ? itemMax : remaining;
                    remaining -= chunk;
                    dropItem(pos_.x, pos_.y, pos_.z, it.id, (uint8_t)chunk, it.meta, 0.0, 0.0,
                             0.0, 10, it.nbt);
                }
            }

            // Drop armor items.
            for (int i = inv_->getSize(); i < inv_->getSize() + 4; ++i) {
                item::ItemStack it = inv_->getItem(i);

                if (it.isEmpty() || it.id == 0 || it.count <= 0) {
                    continue;
                }

                const item::Item* dropDef = item::Item::get(it.id);
                int itemMax = dropDef ? dropDef->maxStackSize() : 64;
                if (itemMax < 1)
                    itemMax = 1;
                int remaining = it.count;
                while (remaining > 0) {
                    int chunk = remaining > itemMax ? itemMax : remaining;
                    remaining -= chunk;
                    dropItem(pos_.x, pos_.y, pos_.z, it.id, (uint8_t)chunk, it.meta, 0.0, 0.0,
                             0.0, 10, it.nbt);
                }
            }

            inv_->clearAll();
        }
    }

    // Reset state after death.
    was_on_fire_ = false;
    fire_ticks_ = 0;
    was_in_lava_ = false;
    fall_distance_ = 0.0f;
    food_saturation_ = 5.0f;
    food_exhaustion_ = 0.0f;
    last_eat_ms_ = 0;
    starve_ticks_ = 0;
    air_ticks_ = 300;

    // A death mid-draw (very common while shooting) must not leave the
    // drawing pose up for viewers nor a stale draw-start tick behind:
    // the next release would otherwise fire from a tick counted before
    // the death, or the stuck flag would swallow the next draw.
    bow_start_action_ = -1;
    if (bow_pose_active_)
        broadcastBowPose(false);
}

void Player::spawnTo(Player* player) {
    if (!player || player == this) {
        return;
    }

    auto ap = std::make_shared<network::protocol::AddPlayerPacket>();

    parseUuidBytes(cfg_.uuid, ap->uuid);

    ap->username = cfg_.username;
    ap->eid = runtimeId();

    ap->x = (float)pos_.x;
    ap->y = (float)pos_.y;
    ap->z = (float)pos_.z;

    ap->yaw = yaw_;
    ap->headYaw = yaw_;
    ap->pitch = pitch_;

    // Carry the held item so the viewer renders it on first sight.
    if (inv_) {
        auto held = inv_->getHeldItem();
        ap->itemId = (int16_t)held.id;
        ap->itemCount = held.count;
        ap->itemDamage = (int16_t)held.meta;
        ap->itemNbt = held.nbt;
    }

    player->sendPacket(ap);

    sendFullEntityData(player);
}

} // namespace cursestone