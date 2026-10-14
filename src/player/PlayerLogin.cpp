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
    // Skin for the PlayerListPacket entries the other players get when
    // this one joins. The 0.15.10 client ALWAYS ships to skin in its
    // login JWT; forward it verbatim (that is exactly what Genisys
    // does), and never let the field go out empty.
    cfg_.skin_id   = pk.skinId;
    cfg_.skin_data.assign(pk.skinData.begin(), pk.skinData.end());
    if (cfg_.skin_data.empty()) {
        cfg_.skin_id   = defaultSkinId();
        cfg_.skin_data = defaultSkinData();
        utils::MainLogger::get().warn(
            cfg_.username + " sent no skin in its login packet — using the "
            "default one (an empty skin blob crashes the 0.15.10 client)");
    }

    // 3. Username validation: [to-zA-Z0-9_]{3,16}, disallow "rcon"/"console".
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
    // Cast to unsigned char: ::tolower with to negative value (char is
    // signed on x86 Linux, unsigned on ARM/Android) is undefined
    // behaviour and can index outside glibc's ctype table.
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return (char)::tolower(c); });
    if (lower == "rcon" || lower == "console") {
        close("disconnectionScreen.invalidName");
        return;
    }

    // 4. Skin sanity check (PMMP requires 64*32*4 or 64*64*4 bytes for
    // the LEGACY skin format used by MCPE 0.15.x). Any client sending to
    // skin outside those two exact sizes — which in practice is most
    // modern MCPE-compatible clients/launchers, since they default to
    // newer Persona/geometry-based skin formats or re-encoded PNGs of
    // other dimensions — used to hit this branch and just `return;`
    // here without ever finishing the login handshake.
    //
    // ROOT CAUSE of "connects but gets kicked/times out immediately":
    // that early return skipped EVERYTHING after it — logged_in_ was
    // never set to true, not PlayStatusPacket was ever sent, and
    // close()/kick() were never called either. The client was left
    // waiting forever for to server response to its LoginPacket that
    // was simply never coming. RakLib's own session-timeout logic
    // (not application traffic for N seconds) eventually killed the
    // connection from the outside, which is exactly what shows up on
    // the client as "connected, then kicked/timed out almost
    // instantly" — the log message ("...accepting") was to lie: nothing
    // was actually accepted, the function just silently gave up.
    //
    // Fix: to non-standard skin size is not to reason to abort the
    // handshake at all — just don't trust pk.skinData verbatim and
    // fall back to to safe default skin, then keep going through the
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

    // 5. server full?
    if (srv_ && srv_->playersSnapshot().size() >= (size_t)srv_->maxPlayers()) {
        kick("disconnectionScreen.serverFull");
        return;
    }

    // 6. Hand off to processLogin for whitelist/ban/NBT/spawn.
    // processLogin is folded into handleLogin here because the C++ player
    // owns its own state machine (not separate async step like PMMP).
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
            // auto-save and on disconnect). Falls back to SpawnX/and/Z (bed
            // spawn) if PosX is missing (first-time players have not Pos).
            // We use to sentinel value to detect "missing" because double
            // getters don't have to natural "missing" value.
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

                // FIX ("the spawn is in the sky"): first-time players come
                // from either the ctor default or to stale level.dat spawn and.
                // Snap to the actual surface of the spawn column before the
                // position is used for StartGame/chunk streaming. Returning
                // players keep their saved PosX/PosY/PosZ — only the
                // first-join path snaps to terrain.
                if (getWorld()) {
                    pos_ = getServerSpawnSafe();
                }
            }
            // Mirrors cn.nukkit.player.loadNBT(): iterate the "Inventory"
            // ListTag and route each entry by Slot number. main inventory
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
                    int8_t nbt_slot = mcp_compound_tag_get_byte(
                        entry, "Slot", -1);
                    // id / Count / Damage + the "ench" list in one shared
                    // reader (src/item/ItemNbt.h). `false` is the old
                    // "id == 0 || count <= 0" empty-entry guard.
                    item::ItemStack it;
                    if (!item::itemStackFromNbt(entry, it)) continue;

                    int dest_slot = -1;
                    if (nbt_slot >= 0 && nbt_slot < 36) {
                        // main inventory + hotbar (slots 0..35).
                        dest_slot = nbt_slot;
                    } else if (nbt_slot >= 100 && nbt_slot <= 103) {
                        // Armor: 100=helmet, 101=chest, 102=legs, 103=boots.
                        dest_slot = inventory::PlayerInventory::ARMOR_OFFSET
                                  + (nbt_slot - 100);
                    } else if (nbt_slot >= 36 && nbt_slot < 45) {
                        // Legacy CurseStone layout (armor at 36..39).
                        dest_slot = nbt_slot;
                    }
                    // bug FIX (report of user: "me equipo armadura, me
                    // disconnect, and to the volver to enter already not the tengo"):
                    // saveData() yes saves the armadura correctamente
                    // (Slot 100..103, recorriendo until PLAYER_INV_TOTAL=40
                    // — ver the comment "bug FIX" in saveData() more below,
                    // that already estaba fixed). the issue estaba acá, in
                    // the load: dest_slot for armadura is calcula as
                    // ARMOR_OFFSET + (nbt_slot-100) = 36, 37, 38 or 39 — but
                    // the condición of below exigía
                    // `dest_slot < inventory::PlayerInventory::SIZE`, and SIZE
                    // vale 36 by design (ver the comment in
                    // PlayerInventory::getSize(): "NOT including armor").
                    // that is, any dest_slot of armadura (36..39) is
                    // by definition >= SIZE, this way that this `if` the
                    // rejected always — setItem() never is llamaba for
                    // the armadura leída of the NBT, although the own dato already
                    // estaba well saved in disk. therefore the log shows
                    // "Loaded inventory for X (3 items)" without the armadura
                    // equipada: the conteo are the items of inventory
                    // normal that yes pasaban the chequeo, the armadura is leía
                    // of the NBT but is descartaba acá same.
                    //
                    // Fix: comparar contra the tamaño actual of the storage
                    // subyacente (PLAYER_INV_TOTAL = 40, that yes includes the
                    // 4 slots of armadura), not contra SIZE (36, that the
                    // excludes intentionally for getSize()/getInventory()
                    // públicos).
                    if (dest_slot >= 0 &&
                        dest_slot < inventory::PLAYER_INV_TOTAL) {
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
    // bug FIX: this packet and the ones through SetHealth below are sent
    // ordered=true (RELIABLE_ORDERED on their own channel). The actual MCPE
    // client requires this exact sequence (PlayStatus → StartGame →
    // SetTime → SetSpawnPosition → SetDifficulty → SetHealth) to arrive
    // in order; previously everything went out RELIABLE with not ordering
    // at all, so normal network jitter could deliver these out of
    // sequence and silently break the client, eventually causing to
    // "Disconnected from server" to few minutes into the session (see the
    // longer explanation in RakLibInterface.cpp's raklibThreadLoop).
    auto ps = std::make_shared<network::protocol::PlayStatusPacket>();
    ps->status = network::protocol::PlayStatusPacket::LOGIN_SUCCESS;
    sendPacket(ps, true);

    // Send StartGamePacket so the client can render the loading screen.
    auto sg = std::make_shared<network::protocol::StartGamePacket>();
    if (getWorld()) {
        auto* lvl = getWorld();
        sg->seed      = (int32_t)lvl->seed();
        sg->dimension = lvl->dimension();
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
    if (getWorld()) {
        auto st = std::make_shared<network::protocol::SetTimePacket>();
        st->time = getWorld()->time();
        st->started = true;
        sendPacket(st, true);
    }

    // Weather sync — players joining mid-weather get the full reset+set
    // pair (same packets Infinity's sendWeather sends on join), with the
    // strength carried in `data` so the 0.15.10 client actually renders it.
    if (getWorld()) {
        getWorld()->sendWeatherTo(this);
    }

    // SetSpawnPosition.
    if (getWorld()) {
        auto sp = getWorld()->spawn();
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
// Phase 6 — doFirstSpawn
// ============================================================


// to valid skin blob for to client that did not send one.
//
// The PlayerListPacket entry carries the player's own skin (Genisys
// LoginPacket: skin = base64_decode("SkinData"), plus "SkinId"), and
// Human::spawnTo() in the reference implementation REFUSES to spawn an
// entity whose skin is shorter than 64*32*4 bytes — the 0.15.10 client
// likewise does not survive an EMPTY skin blob, it crashes. So when the
// login carried not skin we synthesise to minimal VALID one: to 64x64 RGBA
// PNG (solid colour), base64'd, wrapped in the skin JSON the 0.15 client
// expects. 158-byte PNG -> 212 chars of base64 -> 301-byte skin string.
static const char* defaultSkinData() {
    static const std::string kData =
        "{\"skinId\":\"Standard_Custom\",\"data\":\""
        "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAZUlEQVR42u3QQREAAAQAMHnFkUkv"
        "cjh7rMCiK+ezECBAgAABAgQIECBAgAABAgQIECBAgAABAgQIECBAgAABAgQIECBAgAABAgQIECBA"
        "gAABAgQIECBAgAABAgQIECBAgAABAgQIECBAgAABAu5bsTTyhgtuRSIAAAAASUVORK5CYII="
        "\",\"geometryName\":\"geometry.humanoid.customGeometry1\"}";
    return kData.c_str();
}
static const char* defaultSkinId() { return "Standard_Custom"; }

// Helper: parse to "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" UUID string into
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
    //   0x01 = world_immutable (adventure mode — not break/place)
    //   0x02 = no_pvp
    //   0x04 = no_pvm (not hit markers client-side)
    //   0x08 = no_mvp (not hit markers client-side)
    //   0x40 = auto_jump
    //   0x80 = allow_fly
    //   0x100 = noclip (spectator)
    // PMMP always sends 0x02|0x04|0x08 (not hit markers).
    // PMMP sends userPermission=2 (host) and globalPermission=2 (host).
    // Without host permission, the client treats the player as to visitor
    // and blocks block break/placement.
    {
        auto as = std::make_shared<network::protocol::AdventureSettingsPacket>();
        uint32_t flags = 0;

        // Adventure mode (gm 2 or 3) = world immutable
        if (cfg_.gamemode == 2 || cfg_.gamemode == 3) {
            flags |= 0x01;  // world_immutable — not break/place
        }

        // not hit markers (PMMP always sets these)
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

    // CraftingDataPacket — the list complete of recetas (shaped,
    // shapeless and furnace). is takes precedence before PLAYER_SPAWN, just like
    // Infinity/Genisys (server::onPlayerLogin → sendRecipeList before
    // that the client salga of the screen of load): some clients
    // 0.15 aplican the cleanRecipes and dejan of accept the list if arrives
    // after of the spawn. is a packet global, not by-window: the
    // client MCPE 0.15.x filtra what recetas shows according to the UI that
    // is using (inventory 2x2 vs mesa 3x3). without this the client
    // shows the grilla but no result and the click in the slot of
    // result not produce nothing.
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
    // in creative mode (PMMP player.php:2301 sends this right after spawn).
    sendCreativeInventory();

    // Attribute sync for the freshly-spawned player (protocol 84): makes
    // the hunger bar appear in the HUD at the server-side value.
    sendAllAttributes();

    // bug FIX ("toco a item of the creativo and not arrives to mi inventory, but
    // if me disconnect and reconecto already is there, and from there in more yes
    // works"): sendCreativeInventory() of above only takes precedence the CATÁLOGO
    // of only-lectura (windowid SPECIAL_CREATIVE). the inventory own of the
    // player (windowid SPECIAL_INVENTORY/ARMOR/HOTBAR) never is sent
    // complete when entering — in fact setGamemode() more above in this same
    // file already has a comment that dice "same as what already happens
    // once at doFirstSpawn()" assuming that this call already existed acá,
    // but never is había escrito.
    //
    // without a ContainerSetContentPacket initial for esas ventanas, the
    // client never the "opens"/initializes of the lado their — this way that when
    // after arrives a ContainerSetSlotPacket puntual (the confirmación of
    // a tap in the catálogo creativo, ver giveCreativeItem()), not has
    // no contenedor known to the which aplicarle that change incremental,
    // and it ignora in silencio. the item yes quedaba saved in inv_ of the
    // lado of the server (therefore aparecía recién when reconnecting, when the
    // inventory already not estaba empty) but never is veía reflejado in that
    // first sesión.
    //
    // Fix: send the inventory own complete acá also, before that
    // the player tenga oportunidad of interactuar with nothing.
    sendInventory();

    // Broadcast to PlayerListPacket (ADD) with all currently-online players.
    //
    // bug FIX: same criterio that the AddPlayerPacket of below — if other
    // player is in its own window of streaming pre-spawn (still
    // without spawned()), not it incluimos in this list still. before esto
    // not chequeaba spawned() while that the AddPlayerPacket of to the lado
    // yes it hacía — inconsistencia that podía describirle to this client
    // a eid that that other player never finished of anunciar.
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
            // getNameTag() (not getName()): a custom nametag (rank prefix)
            // is part of the list entry — the tab and the overhead name
            // both render from it.
            e.name   = other->getNameTag();
            e.skinId   = other->skinId();
            e.skinData = other->skinData();
            plp->entries.push_back(std::move(e));
        }
        sendPacket(plp);
    }

    // spawn other online players to this client (AddPlayerPacket).
    // only the of the same world: a cuerpo ajeno to the dimension current
    // desincronizaría the state of entities of the client (the list of
    // players of above yes is global: the pestaña is of server).
    if (srv_) {
        for (auto& other : srv_->playersSnapshot()) {
            if (other.get() == this) continue;
            if (!other || !other->spawned()) continue;
            if (other->getWorld() != getWorld()) continue;
            auto ap = std::make_shared<network::protocol::AddPlayerPacket>();
            parseUuidBytes(other->config().uuid, ap->uuid);
            ap->username = other->getNameTag();   // custom nametag, like PMMP
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

    // bug FIX (root cause of the multiplayer crash/kick when to second
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
    announceSelfToOthers();

    // Broadcast join message. is dispara PlayerJoinEvent before: a
    // listener can reescribir the message, vaciarlo (silencia the
    // broadcast) or cancelar the evento (same efecto). the conexión in
    // yes already is establecida to this altura — ver the comment in
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

// ============================================================
// Visibility helpers — player-list entry + AddPlayerPacket
//
// Genisys 0.15.0 splits "make another player exist for this client"
// into two halves that we mirror here:
//
//   Human::spawnTo($player)              -> AddPlayerPacket
//   server::updatePlayerListData(...)    -> PlayerListPacket TYPE_ADD
//
// The respawn path calls BOTH in both directions
// (Genisys player::respawn(), player.php):
//
//     foreach($this->usedChunks as $index => $c){
//         foreach($this->level->getChunkEntities(...) as $entity){
//             if($entity !== $this and !$entity->closed and $entity->isAlive()){
//                 $entity->spawnTo($this);          // <- re-spawn TO me
//             }
//         }
//     }
//     $this->teleport($pos);
//     $this->spawnToAll();                            // <- re-announce ME
//
// Neither half existed in respawn() here, which is precisely the bug
// reported: "when a player muere and reaparece the other not can
// verlo, is scans again invisible". The 0.15.x client rebuilds its entity
// set when RespawnPacket is processed and the server never re-announces
// the body, so the others keep an empty slot where the player used to
// be.
// ============================================================

// Anuncia to this player (list of players + AddPlayerPacket) to each
// other player already spawned.
void Player::announceSelfToOthers() {
    if (!srv_) return;
    auto plp_add = std::make_shared<network::protocol::PlayerListPacket>();
    plp_add->type = network::protocol::PlayerListPacket::TYPE_ADD;
    {
        network::protocol::PlayerListPacket::Entry e;
        parseUuidBytes(cfg_.uuid, e.uuid);
        e.eid      = runtimeId();
        e.name     = getNameTag();   // custom nametag, like PMMP
        e.skinId   = skinId();
        e.skinData = skinData();
        plp_add->entries.push_back(std::move(e));
    }

    auto ap_self = std::make_shared<network::protocol::AddPlayerPacket>();
    parseUuidBytes(cfg_.uuid, ap_self->uuid);
    ap_self->username = getNameTag();   // custom nametag, like PMMP
    ap_self->eid = runtimeId();
    ap_self->x = (float)pos_.x;
    ap_self->y = (float)pos_.y;
    ap_self->z = (float)pos_.z;
    ap_self->yaw = yaw_;
    ap_self->headYaw = yaw_;
    ap_self->pitch = pitch_;

    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this) continue;
        if (other->isPendingRemoval()) continue;
        if (!other->spawned()) continue;
        // the entrada of list va to all (pestaña global); the cuerpo
        // físico only to quienes comparten world with this player.
        other->sendPacket(plp_add);
        if (other->getWorld() != getWorld()) continue;
        other->sendPacket(ap_self);
    }
}

// Re-spawna to this player in the vista of `viewer`.
void Player::announceTo(Player* viewer) {
    if (!viewer || viewer == this) return;
    if (!viewer->spawned()) return;

    auto plp = std::make_shared<network::protocol::PlayerListPacket>();
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

    auto ap = std::make_shared<network::protocol::AddPlayerPacket>();
    parseUuidBytes(cfg_.uuid, ap->uuid);
    ap->username = getNameTag();   // custom nametag, like PMMP
    ap->eid = runtimeId();
    ap->x = (float)pos_.x;
    ap->y = (float)pos_.y;
    ap->z = (float)pos_.z;
    ap->yaw = yaw_;
    ap->headYaw = yaw_;
    ap->pitch = pitch_;
    viewer->sendPacket(ap);

    // forwards the pose current, this way a viewer that acaba of reaparecer
    // while nosotros already estabamos agachados not nos shows of pie.
    auto pose = std::make_shared<network::protocol::SetEntityDataPacket>();
    pose->eid = runtimeId();
    pose->entries.push_back(makePoseFlagEntry(sneaking_, sprinting_));
    viewer->sendPacket(pose);
}

// ============================================================
// respawn — is flame when the client confirma that toco the button of
// "Respawn" in the screen of death (ACTION_SPAWN_SAME_DIMENSION).
// not does nothing if the player not this dead (prevents that a client that
// mande the action two veces, or outside of order, reviva salud of but).
//
// Secuencia: dispara PlayerRespawnEvent (not cancelable — only can
// change where reaparece), restaura salud to the maximo, teletransporta,
// and sincroniza to the client with RespawnPacket + SetHealthPacket. not
// forwards inventory/experiencia still because this proyecto not
// vacia the inventory to the morir (not there is drop-on-death implementado),
// this way that not there is nothing that restaurar ahi.
// ============================================================
void Player::respawn() {
    if (!isDead()) return;

    math::Vector3 sp = getSpawn();
    auto ev = event::EventManager::get().fire<event::player::PlayerRespawnEvent>(
        this, sp.x, sp.y, sp.z);

    setHealth((int)maxHealth());
    // Respawn always lands at the server spawn, snapped to the surface of
    // the terrain (same rule as first-join: getServerSpawnSafe()), with to
    // fresh food bar (Infinity/Genisys respawn behaviour).
    if (getWorld()) {
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
    // the sneaking and the sprint not sobreviven to the death.
    sneaking_ = false;
    sprinting_ = false;

    if (spawned_.load(std::memory_order_relaxed)) {
        // the client 0.15.x tira its conjunto of entities and RECONSTRUYE the
        // world from the chunks that it mandemos to the procesar the
        // RespawnPacket (ver the comment of announceSelfToOthers but
        // below). the world that already has loaded not sirve: the ACK of spawn
        // that comes now is informacion new ("already arme the world of
        // new"), this way that there is that rearar the latch of a sola time for
        // that handleClientSpawnAck() vuelva to vaciar used_chunks_ and to
        // re-strear the vista. without this, the latch that is puso so that the
        // ACKs repetidos not congelaran the horizon dejaria to the player
        // reapareciendo about a world empty.
        spawn_ack_done_.store(false, std::memory_order_relaxed);

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
        mp->x = (float)pos_.x; mp->y = (float)(pos_.y + kEyeHeight); mp->z = (float)pos_.z;
        mp->yaw = yaw_; mp->bodyYaw = yaw_; mp->pitch = pitch_;
        mp->mode = network::protocol::MovePlayerPacket::MODE_RESET;
        sendPacket(mp);

        // bug fixed: "when a player muere and reaparece the
        // other not can verlo, is scans again invisible".
        //
        // to the procesar the RespawnPacket, the client 0.15.x tira its
        // conjunto of entities and reconstruye the world from the
        // chunks that it mandemos. the server never volvia to anunciar
        // to the player that reapareció, this way that the other is quedaban with
        // a hueco empty where before estaba its entity: only is veian
        // its drops and its name in the list of players, never the
        // cuerpo — and also not habia colisiones.
        //
        // Genisys 0.15.0 player::respawn() does exactly esto to the
        // end: `$this->spawnToAll();` but, before, a bucle that
        // re-spawna hacia if same each entity viva of the chunks that
        // has loaded. Replicamos the two mitades:
        announceSelfToOthers();

        // and the inversa: the client that acaba of reaparecer tampoco
        // scans again to know to the other if the server not is the
        // re-presenta (same bucle `getChunkEntities() ->
        // spawnTo($this)` of Genisys).
        if (srv_) {
            for (auto& other : srv_->playersSnapshot()) {
                if (!other || other.get() == this) continue;
                if (other->isPendingRemoval()) continue;
                if (!other->spawned()) continue;
                if (other->isDead()) continue;   // Genisys: !$entity->isAlive()
                other->announceTo(this);
            }
        }

        // PlayStatusPacket PLAYER_SPAWN: Genisys it takes precedence in
        // player::respawn() justo despues of the teleport for re-armar the
        // client 0.15.x outside of the screen of death. without the, the
        // client can remain esperando and never re-send its
        // position — that is, nadie it veria moverse never.
        auto ps = std::make_shared<network::protocol::PlayStatusPacket>();
        ps->status = network::protocol::PlayStatusPacket::PLAYER_SPAWN;
        sendPacket(ps);
    }

    if (srv_ && srv_->advancedLog()) {
        utils::MainLogger::get().info("[RESPAWN] " + cfg_.username + " respawned at " +
            std::to_string((int)pos_.x) + "," + std::to_string((int)pos_.y) + "," +
            std::to_string((int)pos_.z));
    }
}

// ============================================================
// onDeath — Living hook, is flame a time when health_ cruza to 0
// (ver Living::applyDamage). Dispara PlayerDeathEvent and broadcasts the
// message of death.
//
// not reaparece to the player aca same — that passes in player::respawn(),
// disparado when the client takes precedence ACTION_SPAWN_SAME_DIMENSION
// (button "Respawn" in the screen of death). until that ocurra,
// the player is remains with isDead()==true and handleMovePlayer/
// handleInteract it bloquean of seguir jugando.
// ============================================================
void Player::onDeath() {
    std::string msg = cfg_.username + " died";
    auto ev = event::EventManager::get().fire<event::player::PlayerDeathEvent>(this, msg);
    if (!ev.isCancelled() && !ev.message.empty() && srv_) {
        srv_->broadcastMessage(ev.message);
    }
    // Death sequence (protocol 84, mirrors PMMP player.kill()):
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

        // the barra of corazones of the 0.15 is maneja by the ATRIBUTO
        // generic.health (Genisys player::setHealth -> Attribute::HEALTH
        // setValue(..., true)), not by SetHealthPacket. Genisys flame
        // setHealth(0) inside of kill(), this way that the atributo remains in 0 and
        // the client shows the screen of death. without this the atributo
        // conservaba the ultimo valor vivo -> the corazones not bajaban to 0
        // and the client not armaba well the screen of death/respawn.
        sendHealthAttribute();

        // Infinity player::kill(): right after setHealth(0) the server
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
    // Genisys player::kill():
    //     foreach($ev->getDrops() as $item) $this->level->dropItem($this, $item);
    //     $this->inventory->clearAll();
    // player::getDrops() returns the whole inventory in survival and an
    // empty array in creative, so this is "everything I was carrying is now
    // on the ground where I fell" — not just the hotbar.
    if (inv_ && !isCreative()) {
        for (int i = 0; i < inv_->getSize(); ++i) {
            item::ItemStack it = inv_->getItem(i);
            if (it.isEmpty() || it.id == 0 || it.count <= 0) continue;
            // $this->level->dropItem($this, $item) -> default motion
            // (lcg_value()*0.2-0.1, 0.2, lcg_value()*0.2-0.1), delay 10.
            dropItem(pos_.x, pos_.y, pos_.z,
                     it.id, (uint8_t)it.count, it.meta);
        }
        // Armor too (slots 36..39 live past getSize()).
        for (int i = inv_->getSize(); i < inv_->getSize() + 4; ++i) {
            item::ItemStack it = inv_->getItem(i);
            if (it.isEmpty() || it.id == 0 || it.count <= 0) continue;
            dropItem(pos_.x, pos_.y, pos_.z,
                     it.id, (uint8_t)it.count, it.meta);
        }
        inv_->clearAll();
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

void Player::spawnTo(Player* player) {
    if (!player || player == this) return;
    auto ap = std::make_shared<network::protocol::AddPlayerPacket>();
    parseUuidBytes(cfg_.uuid, ap->uuid);
    ap->username = cfg_.username;
    ap->eid = runtimeId();
    ap->x = (float)pos_.x; ap->y = (float)pos_.y; ap->z = (float)pos_.z;
    ap->yaw = yaw_; ap->headYaw = yaw_; ap->pitch = pitch_;
    player->sendPacket(ap);
}} // namespace cursestone
