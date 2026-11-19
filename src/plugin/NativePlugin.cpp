/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later

// NativePlugin — implementation of the C-ABI plugin backend.
//
// The static API groups (log / scheduler / commands / events / server /
// player / world / data) are shared by every plugin instance; per-plugin
// state reaches them through the CsPluginApi::impl back pointer. Event
// bridges mirror the Lua events.on() surface: same names, same fields,
// same writability rules, so both plugin backends observe identical
// events.
#include "NativePlugin.h"
#include "LuaBindings.h" // apiCompatible() + CURSESTONE_API
#include "NativeCommand.h"
#include "PathSafe.h"
#include "Player.h"
#include "Server.h"
#include "command/ConsoleCommandSender.h"
#include "command/CommandMap.h"
#include "entity/Effect.h"
#include "entity/EntityTypes.h"
#include "event/EventManager.h"
#include "event/block/BlockBreakEvent.h"
#include "event/block/BlockPlaceEvent.h"
#include "event/block/DoorToggleEvent.h"
#include "event/entity/EntityDamageByEntityEvent.h"
#include "event/entity/EntityDamageEvent.h"
#include "event/inventory/InventoryClickEvent.h"
#include "event/inventory/InventoryCloseEvent.h"
#include "event/inventory/InventoryOpenEvent.h"
#include "event/player/PlayerBedEnterEvent.h"
#include "event/player/PlayerBedLeaveEvent.h"
#include "event/player/PlayerChatEvent.h"
#include "event/player/PlayerDeathEvent.h"
#include "event/player/PlayerDropItemEvent.h"
#include "event/player/PlayerInteractEvent.h"
#include "event/player/PlayerItemHeldEvent.h"
#include "event/player/PlayerJoinEvent.h"
#include "event/player/PlayerMoveEvent.h"
#include "event/player/PlayerQuitEvent.h"
#include "event/player/PlayerRespawnEvent.h"
#include "inventory/PlayerInventory.h"
#include "item/ItemNbt.h"
#include "network/protocol/MovePlayerPacket.h"
#include "network/protocol/SetEntityMotionPacket.h"
#include "scheduler/CallbackTask.h"
#include "scheduler/ServerScheduler.h"
#include "utils/MainLogger.hpp"
#include "world/World.h" // complete World for name()/seed()/setBlock()
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>
#include <type_traits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX // keep std::min/std::max usable in the headers below
#endif
#include <windows.h>
#undef VOID // DamageCause::VOID collides with the Win32 VOID macro
#else
#include <dlfcn.h>
#endif

namespace cursestone::plugin {

namespace {

namespace fs = std::filesystem;

utils::MainLogger& log() {
    return utils::MainLogger::get();
}

// The CsPluginApi pointer a plugin received IS the plugin instance's
// apiRoot_ member; the server-internal impl slot carries `this`.
NativePlugin* pluginFromApi(CsPluginApi* api) {
    if (!api || api->abiVersion != CS_PLUGIN_ABI_VERSION)
        return nullptr;
    return static_cast<NativePlugin*>(api->impl);
}

// Copies at most cap-1 bytes of src into out, always NUL-terminated.
// Returns false when out or cap are unusable (never when src is NULL —
// that just writes an empty string).
bool copyOut(const char* src, char* out, uint32_t cap) {
    if (!out || cap == 0)
        return false;
    if (!src)
        src = "";
    size_t n = std::strlen(src);
    if (n >= cap)
        n = cap - 1;
    std::memcpy(out, src, n);
    out[n] = '\0';
    return true;
}

// va_list core of the log group. A malformed or oversized format degrades
// to an empty body; it can never crash the server.
void pluginLogVa(NativePlugin* pl, int level, const char* fmt, va_list ap) {
    if (!pl)
        return;
    char body[900];
    if (fmt && vsnprintf(body, sizeof(body), fmt, ap) < 0)
        body[0] = '\0';
    std::string line = "[" + pl->name() + "] " + body;
    auto& lg = log();
    if (level == 1)
        lg.warn(line);
    else if (level == 2)
        lg.error(line);
    else
        lg.info(line);
}

void pluginLog(NativePlugin* pl, int level, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    pluginLogVa(pl, level, fmt, ap);
    va_end(ap);
}

} // namespace

// guarded callback invocation ///////////////////////////////////////////////

void NativePlugin::invokeGuarded(CsTaskFn fn, void* ud, const std::string& pluginName) {
    if (!fn)
        return;
    try {
        fn(ud);
    } catch (const std::exception& e) {
        log().error("[" + pluginName + "] task callback threw an exception: " + e.what());
    } catch (...) {
        log().error("[" + pluginName + "] task callback threw an unknown exception");
    }
}

void NativePlugin::invokeCommand(CsCommandFn fn, const CsCommandSender* sender, int argc,
                                 const char* const* argv, void* ud, const std::string& pluginName) {
    if (!fn)
        return;
    try {
        fn(sender, argc, argv, ud);
    } catch (const std::exception& e) {
        log().error("[" + pluginName + "] command callback threw an exception: " + e.what());
    } catch (...) {
        log().error("[" + pluginName + "] command callback threw an unknown exception");
    }
}

// static API groups //////////////////////////////////////////////////////////

namespace {

// log: printf-style lines under the "[PluginName]" prefix.
void api_log_info(CsPluginApi* api, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    pluginLogVa(pluginFromApi(api), 0, fmt, ap);
    va_end(ap);
}
void api_log_warn(CsPluginApi* api, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    pluginLogVa(pluginFromApi(api), 1, fmt, ap);
    va_end(ap);
}
void api_log_error(CsPluginApi* api, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    pluginLogVa(pluginFromApi(api), 2, fmt, ap);
    va_end(ap);
}

CsLogApi g_logApi = {api_log_info, api_log_warn, api_log_error};

// scheduler: tasks run on the main thread; ids are 0 on invalid input.
uint64_t api_sched_later(CsPluginApi* api, int32_t ticks, CsTaskFn fn, void* ud) {
    NativePlugin* pl = pluginFromApi(api);
    if (!pl || !fn || ticks < 1)
        return 0;
    return pl->scheduleTask(fn, ud, ticks, false);
}
uint64_t api_sched_every(CsPluginApi* api, int32_t ticks, CsTaskFn fn, void* ud) {
    NativePlugin* pl = pluginFromApi(api);
    if (!pl || !fn || ticks < 1)
        return 0;
    return pl->scheduleTask(fn, ud, ticks, true);
}
bool api_sched_cancel(CsPluginApi* api, uint64_t id) {
    NativePlugin* pl = pluginFromApi(api);
    if (!pl || id == 0)
        return false;
    pl->cancelTask(id);
    return true;
}

CsSchedulerApi g_schedApi = {api_sched_later, api_sched_every, api_sched_cancel};

// commands: same permission vocabulary as the Lua commands.register().
bool api_cmd_add(CsPluginApi* api, const char* name, const char* description, const char* usage,
                 const char* permission, CsCommandFn fn, void* ud) {
    NativePlugin* pl = pluginFromApi(api);
    if (!pl || !fn || !name || !*name)
        return false;

    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });

    NativeCommand::PermMode mode = NativeCommand::PermMode::PUBLIC;
    std::string node;
    if (permission && *permission) {
        std::string p = permission;
        std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) {
            return (char)std::tolower(c);
        });
        if (p == "op" || p == "admin") {
            mode = NativeCommand::PermMode::OP;
        } else if (p != "default" && p != "all" && p != "public" && p != "everyone" &&
                   p != "todos") {
            mode = NativeCommand::PermMode::NODE;
            node = permission; // The node keeps its original casing.
        }
    }

    auto cmd = std::make_unique<NativeCommand>(pl, pl->addCommandEntry(fn, ud), std::move(lower),
                                               description ? description : "",
                                               usage ? usage : "", mode, std::move(node));
    return Server::getInstance().getCommandMap()->registerCommand(std::move(cmd));
}

void api_cmd_reply(const CsCommandSender* sender, const char* msg) {
    if (!sender || !msg)
        return;
    if (sender->isConsole) {
        log().info(msg);
        return;
    }
    Player* p = Server::getInstance().getPlayerExact(sender->name ? sender->name : "");
    if (p)
        p->sendMessage(msg);
}

CsCommandApi g_cmdApi = {api_cmd_add, api_cmd_reply};

// server: world/player lists are resolved per call; nothing caches pointers.
void api_server_broadcast(const char* msg) {
    if (msg)
        Server::getInstance().broadcastMessage(msg);
}
uint32_t api_server_playerCount(void) {
    return (uint32_t)Server::getInstance().playersSnapshot().size();
}
bool api_server_playerNameAt(uint32_t idx, char* out, uint32_t cap) {
    auto list = Server::getInstance().playersSnapshot();
    if (idx >= list.size() || !list[idx])
        return false;
    return copyOut(list[idx]->getName().c_str(), out, cap);
}
bool api_server_isPlayerOnline(const char* name) {
    return name && Server::getInstance().getPlayerExact(name) != nullptr;
}
int api_server_tps(void) {
    return Server::getInstance().getTPS();
}
double api_server_mspt(void) {
    return Server::getInstance().getMSPT();
}
uint64_t api_server_currentTick(void) {
    return Server::getInstance().getCurrentTick();
}
bool api_server_dispatchCommand(const char* consoleLine) {
    if (!consoleLine || !*consoleLine)
        return false;
    ConsoleCommandSender sender;
    CommandMap* map = Server::getInstance().getCommandMap();
    if (!map)
        return false;
    // dispatch() is main-thread by design (see Server::startCommandReader);
    // plugin callbacks run on the main thread, so this is the same lock
    // domain as every other dispatch.
    return map->dispatch(&sender, consoleLine);
}
uint32_t api_server_worldCount(void) {
    return (uint32_t)Server::getInstance().getWorlds().size();
}
bool api_server_worldNameAt(uint32_t idx, char* out, uint32_t cap) {
    auto list = Server::getInstance().getWorlds();
    if (idx >= list.size() || !list[idx])
        return false;
    return copyOut(list[idx]->name().c_str(), out, cap);
}
bool api_server_worldExists(const char* name) {
    return name && Server::getInstance().getWorldByName(name) != nullptr;
}
bool api_server_loadWorld(const char* name, const char* generator) {
    if (!name || !*name)
        return false;
    return Server::getInstance().loadWorld(name, generator && *generator ? generator : "default") !=
           nullptr;
}
bool api_server_unloadWorld(const char* name) {
    return name && Server::getInstance().unloadWorld(name);
}
bool api_server_motd(char* out, uint32_t cap) {
    return copyOut(Server::getInstance().getMotd().c_str(), out, cap);
}

// extension batch 2: server knobs the operator would otherwise set by
// hand — capacity, difficulty, whitelist and name bans, plus the tip and
// popup broadcast flavors.
int api_server_maxPlayers(void) {
    return Server::getInstance().maxPlayers();
}
int api_server_difficulty(void) {
    return Server::getInstance().difficulty();
}
bool api_server_whitelistEnabled(void) {
    return Server::getInstance().whitelistEnabled();
}
Player* serverPlayerForListOps(const char* name) {
    if (!name || !*name)
        return nullptr;
    return Server::getInstance().getPlayerExact(name);
}
bool api_server_isBanned(const char* name) {
    Player* p = serverPlayerForListOps(name);
    return p && p->isBanned();
}
bool api_server_setBanned(const char* name, bool value) {
    Player* p = serverPlayerForListOps(name);
    if (!p)
        return false;
    p->setBanned(value); // The server kicks banned online players itself.
    return true;
}
void api_server_broadcastTip(const char* msg) {
    if (!msg)
        return;
    for (auto& p : Server::getInstance().playersSnapshot())
        if (p)
            p->sendTip(msg);
}
void api_server_broadcastPopup(const char* msg) {
    if (!msg)
        return;
    for (auto& p : Server::getInstance().playersSnapshot())
        if (p)
            p->sendPopup(msg);
}

CsServerApi g_serverApi = {api_server_broadcast,
                           api_server_playerCount,
                           api_server_playerNameAt,
                           api_server_isPlayerOnline,
                           api_server_tps,
                           api_server_mspt,
                           api_server_currentTick,
                           api_server_dispatchCommand,
                           api_server_worldCount,
                           api_server_worldNameAt,
                           api_server_worldExists,
                           api_server_loadWorld,
                           api_server_unloadWorld,
                           api_server_motd,
                           api_server_maxPlayers,
                           api_server_difficulty,
                           api_server_whitelistEnabled,
                           api_server_isBanned,
                           api_server_setBanned,
                           api_server_broadcastTip,
                           api_server_broadcastPopup};

// player: every call resolves the live player by exact name — the same
// rule the Lua bindings follow, so nothing ever holds a stale Player*.
Player* findOnlinePlayer(const char* name) {
    if (!name)
        return nullptr;
    return Server::getInstance().getPlayerExact(name);
}

bool api_player_sendMessage(const char* name, const char* msg) {
    Player* p = findOnlinePlayer(name);
    if (!p || !msg)
        return false;
    p->sendMessage(msg);
    return true;
}
bool api_player_sendTip(const char* name, const char* msg) {
    Player* p = findOnlinePlayer(name);
    if (!p || !msg)
        return false;
    p->sendTip(msg);
    return true;
}
bool api_player_sendPopup(const char* name, const char* msg) {
    Player* p = findOnlinePlayer(name);
    if (!p || !msg)
        return false;
    p->sendPopup(msg);
    return true;
}
bool api_player_kick(const char* name, const char* reason) {
    Player* p = findOnlinePlayer(name);
    if (!p)
        return false;
    p->kick(reason && *reason ? reason : "Expulsado");
    return true;
}
bool api_player_kill(const char* name) {
    Player* p = findOnlinePlayer(name);
    if (!p || p->isDead())
        return false;
    p->setHealth(0);
    return true;
}
bool api_player_isOp(const char* name) {
    Player* p = findOnlinePlayer(name);
    return p && p->isOp();
}
int api_player_getHealth(const char* name) {
    Player* p = findOnlinePlayer(name);
    return p ? (int)p->health() : -1;
}
bool api_player_setHealth(const char* name, int hp) {
    Player* p = findOnlinePlayer(name);
    if (!p)
        return false;
    p->setHealth(hp);
    return true;
}
bool api_player_teleport(const char* name, double x, double y, double z) {
    Player* p = findOnlinePlayer(name);
    if (!p)
        return false;
    p->teleport(x, y, z);
    return true;
}
bool api_player_teleportWorld(const char* name, const char* world, double x, double y, double z) {
    Player* p = findOnlinePlayer(name);
    if (!p || !world)
        return false;
    World* target = Server::getInstance().getWorldByName(world);
    if (!target)
        return false;
    return p->switchWorld(target, x, y, z, false);
}
bool api_player_getWorld(const char* name, char* out, uint32_t cap) {
    Player* p = findOnlinePlayer(name);
    if (!p || !p->getWorld())
        return false;
    return copyOut(p->getWorld()->name().c_str(), out, cap);
}
bool api_player_getPosition(const char* name, double out[3]) {
    Player* p = findOnlinePlayer(name);
    if (!p || !out)
        return false;
    const math::Vector3& pos = p->getNextPosition();
    out[0] = pos.x;
    out[1] = pos.y;
    out[2] = pos.z;
    return true;
}
bool api_player_getRotation(const char* name, float out[2]) {
    Player* p = findOnlinePlayer(name);
    if (!p || !out)
        return false;
    out[0] = p->yaw();
    out[1] = p->pitch();
    return true;
}
bool api_player_setRotation(const char* name, float yaw, float pitch) {
    Player* p = findOnlinePlayer(name);
    if (!p)
        return false;
    // Camera control: server state first, then the MODE_RESET
    // MovePlayerPacket re-sync (same shape the anti-cheat revert uses).
    p->setRotation(yaw, pitch);
    const math::Vector3& pos = p->getNextPosition();
    p->sendPosition(pos.x, pos.y, pos.z, (double)yaw, (double)pitch,
                    network::protocol::MovePlayerPacket::MODE_RESET);
    return true;
}
bool api_player_setMotion(const char* name, double x, double y, double z) {
    Player* p = findOnlinePlayer(name);
    if (!p)
        return false;
    // Server state first, then SetEntityMotionPacket with eid=0 — the
    // protocol-84 self-motion form, exactly like the Lua binding.
    p->setMotion(math::Vector3{x, y, z});
    auto pk = std::make_shared<network::protocol::SetEntityMotionPacket>();
    pk->entities.push_back({0, (float)x, (float)y, (float)z});
    p->sendPacket(pk);
    return true;
}
bool api_player_setPermission(const char* name, const char* node, bool value) {
    Player* p = findOnlinePlayer(name);
    if (!p || !node)
        return false;
    p->setPermission(node, value);
    return true;
}
bool api_player_hasPermission(const char* name, const char* node) {
    Player* p = findOnlinePlayer(name);
    return p && node && p->hasPermission(node);
}
bool api_player_giveItem(const char* name, int32_t id, int32_t count, int32_t meta,
                         const char* customName) {
    Player* p = findOnlinePlayer(name);
    if (!p || !p->getInventory())
        return false;
    if (id <= 0 || id > 65535 || count < 1)
        return false;
    if (count > 2304)
        count = 2304; // 36 slots * 64 — keep it sane, like the Lua binding.

    item::ItemStack st;
    st.id = (uint16_t)id;
    st.count = count;
    st.meta = (uint8_t)(meta & 0xFF);
    if (customName && *customName)
        st.nbt = item::buildDisplayNbt(customName);

    auto leftovers = p->getInventory()->addItem({st});
    bool fit = leftovers.empty() || leftovers[0].isEmpty() || leftovers[0].count < count;
    p->sendInventory(); // Full resync — the client renders the new stack.
    return fit;
}
bool api_player_setNameTag(const char* name, const char* tag) {
    Player* p = findOnlinePlayer(name);
    if (!p || !tag)
        return false;
    p->setNameTag(tag);
    return true;
}
bool api_player_getNameTag(const char* name, char* out, uint32_t cap) {
    Player* p = findOnlinePlayer(name);
    if (!p)
        return false;
    return copyOut(p->getNameTag().c_str(), out, cap);
}
bool api_player_setDisplayName(const char* name, const char* dn) {
    Player* p = findOnlinePlayer(name);
    if (!p || !dn)
        return false;
    p->setDisplayName(dn);
    return true;
}
bool api_player_getDisplayName(const char* name, char* out, uint32_t cap) {
    Player* p = findOnlinePlayer(name);
    if (!p)
        return false;
    return copyOut(p->getDisplayName().c_str(), out, cap);
}

// extension batch 2: gamemode, effects, XP, address, spawn point and
// fire — the per-player state a minigame or utility plugin touches most.
int api_player_getGamemode(const char* name) {
    Player* p = findOnlinePlayer(name);
    return p ? p->gamemode() : -1;
}
bool api_player_setGamemode(const char* name, int gm) {
    Player* p = findOnlinePlayer(name);
    if (!p || gm < 0 || gm > 3)
        return false;
    p->setGamemode(gm);
    return true;
}
bool api_player_addEffect(const char* name, int32_t effectId, int32_t durationTicks,
                          int32_t amplifier) {
    Player* p = findOnlinePlayer(name);
    if (!p || effectId < 1 || effectId > 255 || durationTicks < 1 || amplifier < 0)
        return false;
    // Only ids the server actually defines pass: unknown ids would reach
    // the client as a nameless effect blob.
    if (!*entity::Effect::nameOf((uint8_t)effectId))
        return false;
    int amp = amplifier > 255 ? 255 : amplifier;
    p->addEffect(std::make_shared<entity::Effect>(entity::Effect::create((uint8_t)effectId, durationTicks, amp)));
    return true;
}
bool api_player_removeEffect(const char* name, int32_t effectId) {
    Player* p = findOnlinePlayer(name);
    if (!p || effectId < 1 || effectId > 255)
        return false;
    p->removeEffect((uint8_t)effectId);
    return true;
}
int api_player_getExperienceLevel(const char* name) {
    Player* p = findOnlinePlayer(name);
    return p ? p->experienceLevel() : -1;
}
bool api_player_setExperience(const char* name, int32_t exp, int32_t level) {
    Player* p = findOnlinePlayer(name);
    if (!p || exp < 0 || exp > 100 || level < 0)
        return false;
    p->setExperience(exp, level);
    return true;
}
bool api_player_getAddress(const char* name, char* out, uint32_t cap) {
    Player* p = findOnlinePlayer(name);
    if (!p)
        return false;
    return copyOut(p->getAddress().c_str(), out, cap);
}
bool api_player_getSpawnPoint(const char* name, double out[3]) {
    Player* p = findOnlinePlayer(name);
    if (!p || !out)
        return false;
    math::Vector3 sp = p->getSpawn();
    out[0] = sp.x;
    out[1] = sp.y;
    out[2] = sp.z;
    return true;
}
bool api_player_setSpawnPoint(const char* name, double x, double y, double z) {
    Player* p = findOnlinePlayer(name);
    if (!p)
        return false;
    p->setSpawn((int)std::floor(x), (int)std::floor(y), (int)std::floor(z), true);
    return true;
}
bool api_player_setOnFire(const char* name, int32_t seconds) {
    Player* p = findOnlinePlayer(name);
    if (!p || seconds < 0)
        return false;
    p->setOnFire(seconds);
    return true;
}

CsPlayerApi g_playerApi = {api_player_sendMessage,
                           api_player_sendTip,
                           api_player_sendPopup,
                           api_player_kick,
                           api_player_kill,
                           api_player_isOp,
                           api_player_getHealth,
                           api_player_setHealth,
                           api_player_teleport,
                           api_player_teleportWorld,
                           api_player_getWorld,
                           api_player_getPosition,
                           api_player_getRotation,
                           api_player_setRotation,
                           api_player_setMotion,
                           api_player_setPermission,
                           api_player_hasPermission,
                           api_player_giveItem,
                           api_player_setNameTag,
                           api_player_getNameTag,
                           api_player_setDisplayName,
                           api_player_getDisplayName,
                           api_player_getGamemode,
                           api_player_setGamemode,
                           api_player_addEffect,
                           api_player_removeEffect,
                           api_player_getExperienceLevel,
                           api_player_setExperience,
                           api_player_getAddress,
                           api_player_getSpawnPoint,
                           api_player_setSpawnPoint,
                           api_player_setOnFire};

// world: calls against unloaded worlds fail with 0 / false.
World* findLoadedWorld(const char* name) {
    if (!name)
        return nullptr;
    return Server::getInstance().getWorldByName(name);
}

int64_t api_world_seed(const char* world) {
    World* w = findLoadedWorld(world);
    return w ? (int64_t)w->seed() : 0;
}
int api_world_dimension(const char* world) {
    World* w = findLoadedWorld(world);
    return w ? (int)w->dimension() : -1;
}
int api_world_getTime(const char* world) {
    World* w = findLoadedWorld(world);
    return w ? w->time() : -1;
}
bool api_world_setTime(const char* world, int32_t t) {
    World* w = findLoadedWorld(world);
    if (!w)
        return false;
    int32_t norm = ((t % 24000) + 24000) % 24000;
    w->setTime(norm);
    return true;
}
int api_world_getBlock(const char* world, int32_t x, int32_t y, int32_t z) {
    World* w = findLoadedWorld(world);
    if (!w || y < 0 || y > 255)
        return 0;
    return w->getBlockId(x, y, z);
}
bool api_world_setBlock(const char* world, int32_t x, int32_t y, int32_t z, int32_t id,
                        int32_t meta) {
    World* w = findLoadedWorld(world);
    if (!w || y < 0 || y > 255 || id < 0 || id > 255 || meta < 0 || meta > 15)
        return false;
    // World::setBlock only writes in resident chunks: force the load like
    // the Lua binding so the change is never a silent no-op.
    w->chunks().getChunk(x >> 4, z >> 4);
    w->setBlock(x, y, z, (uint8_t)id, (uint8_t)meta);
    return true;
}
bool api_world_getSpawn(const char* world, double out[3]) {
    World* w = findLoadedWorld(world);
    if (!w || !out)
        return false;
    mcp_vector3_t sp = w->spawn();
    out[0] = sp.x;
    out[1] = sp.y;
    out[2] = sp.z;
    return true;
}
uint32_t api_world_playerCount(const char* world) {
    World* w = findLoadedWorld(world);
    if (!w)
        return 0;
    uint32_t n = 0;
    for (auto& p : Server::getInstance().playersSnapshot())
        if (p && p->getWorld() == w)
            ++n;
    return n;
}

// extension batch 2: block metadata, world spawn, weather, lightning,
// chunk residency, save and item drops — the world-level surface behind
// minigame arenas and event scripting.
int api_world_getBlockMeta(const char* world, int32_t x, int32_t y, int32_t z) {
    World* w = findLoadedWorld(world);
    if (!w || y < 0 || y > 255)
        return 0;
    return w->getBlockData(x, y, z);
}
bool api_world_setSpawnPoint(const char* world, double x, double y, double z) {
    World* w = findLoadedWorld(world);
    if (!w)
        return false;
    w->setSpawn(mcp_vector3_t{x, y, z});
    return true;
}
int api_world_getWeather(const char* world) {
    World* w = findLoadedWorld(world);
    return w ? w->weatherState() : -1;
}
bool api_world_setWeather(const char* world, int32_t state) {
    World* w = findLoadedWorld(world);
    if (!w || state < 0 || state > 2)
        return false;
    w->setWeather(state); // Default duration; the world broadcasts it.
    return true;
}
bool api_world_spawnLightning(const char* world, double x, double y, double z, bool fire) {
    World* w = findLoadedWorld(world);
    if (!w)
        return false;
    w->spawnLightning(x, y, z, fire);
    return true;
}
bool api_world_isChunkLoaded(const char* world, int32_t cx, int32_t cz) {
    World* w = findLoadedWorld(world);
    return w && w->isChunkLoaded(cx, cz);
}
bool api_world_save(const char* world) {
    World* w = findLoadedWorld(world);
    if (!w)
        return false;
    return w->save() == 0;
}
bool api_world_spawnItem(const char* world, double x, double y, double z, int32_t id, int32_t count,
                         int32_t meta) {
    World* w = findLoadedWorld(world);
    if (!w || id <= 0 || id > 65535 || count < 1 || count > 64 || meta < 0 || meta > 15)
        return false;
    w->dropItemAt(x, y, z, (uint16_t)id, (uint8_t)count, (uint16_t)meta);
    return true;
}
bool api_world_spawnParticle(const char* world, double x, double y, double z, int32_t type,
                             int32_t data) {
    World* w = findLoadedWorld(world);
    if (!w)
        return false;
    return w->spawnParticle(type, (float)x, (float)y, (float)z, data);
}
bool api_world_spawnBlockParticles(const char* world, double x, double y, double z,
                                   int32_t blockId, int32_t meta) {
    World* w = findLoadedWorld(world);
    if (!w || blockId < 0 || blockId > 255 || meta < 0 || meta > 15)
        return false;
    w->broadcastBlockParticles((int32_t)x, (int32_t)y, (int32_t)z, (uint8_t)blockId,
                               (uint8_t)meta);
    return true;
}
bool api_world_spawnSplashParticles(const char* world, double x, double y, double z,
                                    int32_t rgb) {
    World* w = findLoadedWorld(world);
    if (!w || rgb < 0 || rgb > 0xFFFFFF)
        return false;
    return w->spawnSplashParticles((float)x, (float)y, (float)z, (uint32_t)rgb);
}
int64_t api_world_spawnFloatingText(const char* world, double x, double y, double z,
                                    const char* text, int32_t ttlTicks) {
    World* w = findLoadedWorld(world);
    if (!w || !text)
        return -1;
    return w->spawnFloatingText(x, y, z, std::string(text), ttlTicks);
}
bool api_world_moveFloatingText(const char* world, int64_t id, double x, double y, double z) {
    World* w = findLoadedWorld(world);
    return w && w->moveFloatingText(id, x, y, z);
}
bool api_world_removeFloatingText(const char* world, int64_t id) {
    World* w = findLoadedWorld(world);
    return w && w->removeFloatingText(id);
}

// extension batch 6: floating text re-text — mirrors the Lua
// setFloatingText; the line diff happens inside the particle.
bool api_world_setFloatingText(const char* world, int64_t id, const char* text) {
    World* w = findLoadedWorld(world);
    if (!w || !text)
        return false;
    return w->setFloatingText(id, std::string(text));
}

// extension batch 5: standing mobs — the same surface the Lua world
// object exposes. Type ids come from the shared entity registry so an
// id the 0.15.10 client cannot render never reaches the wire.
int64_t api_world_spawnEntity(const char* world, int32_t type, double x, double y,
                              double z, float yaw, float pitch) {
    World* w = findLoadedWorld(world);
    if (!w)
        return -1;
    return w->spawnMob(type, x, y, z, yaw, pitch);
}
bool api_world_moveEntity(const char* world, int64_t id, double x, double y, double z,
                          float yaw, float pitch) {
    World* w = findLoadedWorld(world);
    return w && w->moveMob(id, x, y, z, yaw, pitch);
}
bool api_world_removeEntity(const char* world, int64_t id) {
    World* w = findLoadedWorld(world);
    return w && w->removeMob(id);
}
bool api_world_getEntity(const char* world, int64_t id, int32_t* outType, double outPos[3],
                         float outRot[2]) {
    World* w = findLoadedWorld(world);
    if (!w || !outType || !outPos || !outRot)
        return false;
    const world::MobEntity* m = w->mob(id);
    if (!m)
        return false;
    *outType = m->type;
    outPos[0] = m->x;
    outPos[1] = m->y;
    outPos[2] = m->z;
    outRot[0] = m->yaw;
    outRot[1] = m->pitch;
    return true;
}
bool api_entity_typeFromName(const char* name, int32_t* outId) {
    if (!name || !outId)
        return false;
    return entity::mobTypeFromName(name, outId);
}
const char* api_entity_typeName(int32_t type) {
    return entity::mobTypeName(type);
}

CsWorldApi g_worldApi = {
    api_world_seed,
    api_world_dimension,
    api_world_getTime,
    api_world_setTime,
    api_world_getBlock,
    api_world_setBlock,
    api_world_getSpawn,
    api_world_playerCount,
    api_world_getBlockMeta,
    api_world_setSpawnPoint,
    api_world_getWeather,
    api_world_setWeather,
    api_world_spawnLightning,
    api_world_isChunkLoaded,
    api_world_save,
    api_world_spawnItem,
    api_world_spawnParticle,
    api_world_spawnBlockParticles,
    api_world_spawnSplashParticles,
    api_world_spawnFloatingText,
    api_world_moveFloatingText,
    api_world_removeFloatingText,
    api_world_spawnEntity,
    api_world_moveEntity,
    api_world_removeEntity,
    api_world_getEntity,
    api_entity_typeFromName,
    api_entity_typeName,
    api_world_setFloatingText,
};

// data: the per-plugin data_pl/<plugin> sandbox, same rules as data.*.
bool dataResolve(CsPluginApi* api, const char* rel, std::string& full) {
    NativePlugin* pl = pluginFromApi(api);
    std::string clean;
    if (!pl || !sanitizeRelPath(rel ? rel : "", clean))
        return false;
    full = pl->dataDir() + "/" + clean;
    return true;
}

bool api_data_exists(CsPluginApi* api, const char* rel) {
    std::string full;
    std::error_code ec;
    return dataResolve(api, rel, full) && fs::exists(full, ec);
}
bool api_data_readFile(CsPluginApi* api, const char* rel, void** outData, uint32_t* outSize) {
    std::string full;
    if (!dataResolve(api, rel, full) || !outData || !outSize)
        return false;
    std::ifstream in(full, std::ios::binary);
    if (!in)
        return false;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad())
        return false;
    // malloc/free pair so plugins release the buffer with data.free()
    // regardless of their own allocator.
    void* buf = std::malloc(content.size() ? content.size() : 1);
    if (!buf)
        return false;
    std::memcpy(buf, content.data(), content.size());
    *outData = buf;
    *outSize = (uint32_t)content.size();
    return true;
}
bool api_data_writeFile(CsPluginApi* api, const char* rel, const void* data, uint32_t size) {
    std::string full;
    if (!dataResolve(api, rel, full) || (!data && size > 0))
        return false;
    std::error_code ec;
    fs::create_directories(fs::path(full).parent_path(), ec);
    ec.clear();
    std::ofstream outF(full, std::ios::binary | std::ios::trunc);
    if (!outF)
        return false;
    if (size > 0)
        outF.write((const char*)data, (std::streamsize)size);
    return !outF.fail();
}
bool api_data_remove(CsPluginApi* api, const char* rel) {
    std::string full;
    std::error_code ec;
    return dataResolve(api, rel, full) && fs::remove(full, ec);
}
uint32_t api_data_list(CsPluginApi* api, const char* rel, char* out, uint32_t cap) {
    std::string full;
    if (!dataResolve(api, rel, full) || !out || cap == 0) {
        if (out && cap)
            out[0] = '\0';
        return 0;
    }
    std::error_code ec;
    if (!fs::exists(full, ec)) {
        out[0] = '\0';
        return 0;
    }
    uint32_t count = 0;
    std::string joined;
    for (auto& entry : fs::directory_iterator(full, ec)) {
        std::string nm = entry.path().filename().string();
        if (nm.empty())
            continue;
        ++count;
        if (!joined.empty())
            joined += '\n';
        joined += nm;
        if (joined.size() >= cap - 1)
            break; // Buffer full: stop appending, count stays best-effort.
    }
    copyOut(joined.c_str(), out, cap);
    return count;
}
void api_data_free(void* p) {
    std::free(p);
}

CsDataApi g_dataApi = {api_data_exists, api_data_readFile, api_data_writeFile,
                       api_data_remove, api_data_list,     api_data_free};

// event bridges //////////////////////////////////////////////////////////////

// Strings handed to the callback point into this scratch; it lives in the
// listener lambda and outlives the plugin callback.
struct EventScratch {
    std::string player, entity, damager, inventory, action;
};

void setEntityField(std::string& dst, cursestone::entity::Entity* e) {
    if (!e) {
        dst = "unknown";
        return;
    }
    if (auto* pl = dynamic_cast<Player*>(e))
        dst = pl->getName();
    else
        dst = "eid:" + std::to_string((long long)e->runtimeId());
}

const char* damageCauseName(event::entity::DamageCause c) {
    using event::entity::DamageCause;
    switch (c) {
    case DamageCause::ENTITY_ATTACK:
        return "entity_attack";
    case DamageCause::CONTACT:
        return "contact";
    case DamageCause::FALL:
        return "fall";
    case DamageCause::LAVA:
        return "lava";
    case DamageCause::FIRE:
        return "fire";
    case DamageCause::FIRE_TICK:
        return "fire_tick";
    case DamageCause::DROWNING:
        return "drowning";
    case DamageCause::CACTUS:
        return "cactus";
    case DamageCause::STARVATION:
        return "starvation";
    case DamageCause::VOID:
        return "void";
    case DamageCause::PROJECTILE:
        return "projectile";
    case DamageCause::MAGIC:
        return "magic";
    case DamageCause::CUSTOM:
        return "custom";
    }
    return "custom";
}

// Templated fills read the fields each concrete event actually carries;
// the event classes do not share a Player/Block base for every case.
template <typename E>
void fillPlayerOf(CsEvent* ev, E& e, EventScratch& s) {
    s.player = e.player ? e.player->getName() : "";
    ev->player = s.player.c_str();
}

template <typename E>
void fillPlayerMsgOf(CsEvent* ev, E& e, EventScratch& s) {
    fillPlayerOf(ev, e, s);
    std::snprintf(ev->message, CS_EVENT_MESSAGE_MAX, "%s", e.message.c_str());
}

template <typename E>
void readBackMsgOf(CsEvent* ev, E* e) {
    e->message = ev->message;
}

template <typename E>
void fillBlockOf(CsEvent* ev, E& e, EventScratch& s) {
    fillPlayerOf(ev, e, s);
    ev->x = e.pos.x;
    ev->y = e.pos.y;
    ev->z = e.pos.z;
    ev->blockId = e.block_id;
}

template <typename E>
void fillPosOf(CsEvent* ev, E& e, EventScratch& s) {
    fillPlayerOf(ev, e, s);
    ev->x = e.x;
    ev->y = e.y;
    ev->z = e.z;
}

template <typename E>
void fillDamageOf(CsEvent* ev, E& e, EventScratch& s) {
    setEntityField(s.entity, e.entity);
    ev->player = s.entity.c_str(); // `player` carries the victim for damage events.
    ev->amount = e.amount;
    ev->cause = damageCauseName(e.cause);
}

void readBackAmountOf(CsEvent* ev, event::entity::EntityDamageEvent* e) {
    e->amount = ev->amount > 0 ? (float)ev->amount : 0.0f;
}

void fillItemStackOf(CsEvent* ev, const item::ItemStack& st) {
    ev->itemId = st.id;
    ev->itemMeta = st.meta;
    ev->itemCount = st.count;
}

// Registration closure: builds the CsEvent snapshot, runs the callback
// guarded, then applies cancellation and the writable fields back.
template <typename EventT>
void listenNativeEvent(NativePlugin* pl, const char* name, CsEventFn fn, void* ud,
                       void (*fill)(CsEvent*, EventT&, EventScratch&),
                       void (*readBack)(CsEvent*, EventT*) = nullptr) {
    event::EventManager::get().listen<EventT>(pl, [pl, name, fn, ud, fill, readBack](EventT& e) {
        if (!pl->isEnabled())
            return;
        CsEvent ev{};
        ev.type = name;
        EventScratch scratch;
        fill(&ev, e, scratch);

        const std::string pluginName = pl->name();
        bool ran = false;
        try {
            fn(&ev, ud);
            ran = true;
        } catch (const std::exception& ex) {
            log().error("[" + pluginName + "] event '" + name +
                        "' callback threw an exception: " + ex.what());
        } catch (...) {
            log().error("[" + pluginName + "] event '" + name +
                        "' callback threw an unknown exception");
        }
        if (!ran)
            return;

        if (ev.cancelled) {
            if constexpr (std::is_base_of_v<event::Cancellable, EventT>)
                e.setCancelled(true);
        }
        if (readBack)
            readBack(&ev, &e);
    });
}

// api_events_on — same dispatch chain as l_events_on in Lua.
bool api_events_on(CsPluginApi* api, const char* eventName, CsEventFn fn, void* ud) {
    NativePlugin* pl = pluginFromApi(api);
    if (!pl || !eventName || !fn)
        return false;
    const std::string n = eventName;

    if (n == "PlayerJoin") {
        listenNativeEvent<event::player::PlayerJoinEvent>(
            pl, eventName, fn, ud, fillPlayerMsgOf<event::player::PlayerJoinEvent>,
            readBackMsgOf<event::player::PlayerJoinEvent>);
    } else if (n == "PlayerQuit") {
        listenNativeEvent<event::player::PlayerQuitEvent>(
            pl, eventName, fn, ud, fillPlayerMsgOf<event::player::PlayerQuitEvent>,
            readBackMsgOf<event::player::PlayerQuitEvent>);
    } else if (n == "PlayerChat") {
        listenNativeEvent<event::player::PlayerChatEvent>(
            pl, eventName, fn, ud, fillPlayerMsgOf<event::player::PlayerChatEvent>,
            readBackMsgOf<event::player::PlayerChatEvent>);
    } else if (n == "PlayerDeath") {
        listenNativeEvent<event::player::PlayerDeathEvent>(
            pl, eventName, fn, ud, fillPlayerMsgOf<event::player::PlayerDeathEvent>,
            readBackMsgOf<event::player::PlayerDeathEvent>);
    } else if (n == "BlockBreak") {
        listenNativeEvent<event::block::BlockBreakEvent>(pl, eventName, fn, ud,
                                                         fillBlockOf<event::block::BlockBreakEvent>);
    } else if (n == "BlockPlace") {
        listenNativeEvent<event::block::BlockPlaceEvent>(pl, eventName, fn, ud,
                                                         fillBlockOf<event::block::BlockPlaceEvent>);
    } else if (n == "EntityDamage") {
        listenNativeEvent<event::entity::EntityDamageEvent>(pl, eventName, fn, ud,
                                                            fillDamageOf<event::entity::EntityDamageEvent>,
                                                            readBackAmountOf);
    } else if (n == "EntityDamageByEntity") {
        auto fill = [](CsEvent* ev, event::entity::EntityDamageByEntityEvent& e, EventScratch& s) {
            fillDamageOf(ev, e, s);
            setEntityField(s.damager, e.damager);
            ev->damager = s.damager.c_str();
        };
        auto readBack = [](CsEvent* ev, event::entity::EntityDamageByEntityEvent* e) {
            readBackAmountOf(ev, e); // Writes through the EntityDamageEvent base.
        };
        listenNativeEvent<event::entity::EntityDamageByEntityEvent>(pl, eventName, fn, ud, +fill,
                                                                    +readBack);
    } else if (n == "PlayerRespawn") {
        auto fill = [](CsEvent* ev, event::player::PlayerRespawnEvent& e, EventScratch& s) {
            fillPlayerOf(ev, e, s);
            ev->x = e.respawnX;
            ev->y = e.respawnY;
            ev->z = e.respawnZ;
        };
        listenNativeEvent<event::player::PlayerRespawnEvent>(pl, eventName, fn, ud, +fill);
    } else if (n == "PlayerBedEnter" || n == "PlayerSleepInBed") {
        listenNativeEvent<event::player::PlayerBedEnterEvent>(
            pl, "PlayerBedEnter", fn, ud, fillPosOf<event::player::PlayerBedEnterEvent>);
    } else if (n == "PlayerBedLeave") {
        listenNativeEvent<event::player::PlayerBedLeaveEvent>(
            pl, eventName, fn, ud, fillPosOf<event::player::PlayerBedLeaveEvent>);
    } else if (n == "DoorToggle") {
        auto fill = [](CsEvent* ev, event::block::DoorToggleEvent& e, EventScratch& s) {
            fillPlayerOf(ev, e, s);
            ev->x = e.x;
            ev->y = e.y;
            ev->z = e.z;
        };
        listenNativeEvent<event::block::DoorToggleEvent>(pl, eventName, fn, ud, +fill);
    } else if (n == "PlayerInteract") {
        auto fill = [](CsEvent* ev, event::player::PlayerInteractEvent& e, EventScratch& s) {
            fillPlayerOf(ev, e, s);
            ev->x = e.blockX;
            ev->y = e.blockY;
            ev->z = e.blockZ;
            ev->face = e.face;
            const char* act = "left_click_block";
            if (e.action == event::player::PlayerInteractEvent::RIGHT_CLICK_BLOCK)
                act = "right_click_block";
            else if (e.action == event::player::PlayerInteractEvent::RIGHT_CLICK_AIR)
                act = "right_click_air";
            else if (e.action == event::player::PlayerInteractEvent::LEFT_CLICK_AIR)
                act = "left_click_air";
            else if (e.action == event::player::PlayerInteractEvent::PHYSICAL)
                act = "physical";
            s.action = act;
            ev->action = s.action.c_str();
            fillItemStackOf(ev, e.item);
        };
        listenNativeEvent<event::player::PlayerInteractEvent>(pl, eventName, fn, ud, +fill);
    } else if (n == "PlayerMove") {
        auto fill = [](CsEvent* ev, event::player::PlayerMoveEvent& e, EventScratch& s) {
            fillPlayerOf(ev, e, s);
            ev->fromX = e.fromX;
            ev->fromY = e.fromY;
            ev->fromZ = e.fromZ;
            ev->toX = e.toX;
            ev->toY = e.toY;
            ev->toZ = e.toZ;
        };
        listenNativeEvent<event::player::PlayerMoveEvent>(pl, eventName, fn, ud, +fill);
    } else if (n == "PlayerDropItem") {
        auto fill = [](CsEvent* ev, event::player::PlayerDropItemEvent& e, EventScratch& s) {
            fillPlayerOf(ev, e, s);
            fillItemStackOf(ev, e.item);
        };
        listenNativeEvent<event::player::PlayerDropItemEvent>(pl, eventName, fn, ud, +fill);
    } else if (n == "PlayerItemHeld") {
        auto fill = [](CsEvent* ev, event::player::PlayerItemHeldEvent& e, EventScratch& s) {
            fillPlayerOf(ev, e, s);
            fillItemStackOf(ev, e.item);
            ev->slot = e.slot;
        };
        listenNativeEvent<event::player::PlayerItemHeldEvent>(pl, eventName, fn, ud, +fill);
    } else if (n == "InventoryOpen") {
        auto fill = [](CsEvent* ev, event::inventory::InventoryOpenEvent& e, EventScratch& s) {
            s.player = e.who ? e.who->getName() : "";
            ev->player = s.player.c_str();
            s.inventory = e.inv ? e.inv->getName() : "";
            ev->inventory = s.inventory.c_str();
        };
        listenNativeEvent<event::inventory::InventoryOpenEvent>(pl, eventName, fn, ud, +fill);
    } else if (n == "InventoryClick") {
        auto fill = [](CsEvent* ev, event::inventory::InventoryClickEvent& e, EventScratch& s) {
            s.player = e.who ? e.who->getName() : "";
            ev->player = s.player.c_str();
            s.inventory = e.inv ? e.inv->getName() : "";
            ev->inventory = s.inventory.c_str();
            ev->slot = e.slot;
            fillItemStackOf(ev, e.item);
        };
        listenNativeEvent<event::inventory::InventoryClickEvent>(pl, eventName, fn, ud, +fill);
    } else if (n == "InventoryClose") {
        auto fill = [](CsEvent* ev, event::inventory::InventoryCloseEvent& e, EventScratch& s) {
            s.player = e.who ? e.who->getName() : "";
            ev->player = s.player.c_str();
            s.inventory = e.inv ? e.inv->getName() : "";
            ev->inventory = s.inventory.c_str();
        };
        listenNativeEvent<event::inventory::InventoryCloseEvent>(pl, eventName, fn, ud, +fill);
    } else {
        return false;
    }
    return true;
}

CsEventApi g_eventApi = {api_events_on};

} // namespace

// NativePlugin lifecycle /////////////////////////////////////////////////////

NativePlugin::NativePlugin(std::string libPath, std::string dataRoot)
    : libPath_(std::move(libPath)), dataRoot_(std::move(dataRoot)) {
    fs::path p(libPath_);
    stem_ = p.stem().string(); // HelloNative from HelloNative.so
    pluginsDir_ = p.parent_path().string();
    if (pluginsDir_.empty())
        pluginsDir_ = ".";
    dataDir_ = dataRoot_ + "/" + stem_;
    name_ = stem_; // Descriptor name replaces this after openLibrary().
}

NativePlugin::~NativePlugin() {
    if (enabled_)
        disable();
    if (loadedOk_ && vtable_ && vtable_->onUnload) {
        std::string pluginName = name_;
        void (*onUnload)() = vtable_->onUnload;
        try {
            onUnload();
        } catch (const std::exception& e) {
            log().error("[" + pluginName + "] onUnload threw an exception: " + e.what());
        } catch (...) {
            log().error("[" + pluginName + "] onUnload threw an unknown exception");
        }
    }
    if (lib_) {
#if defined(_WIN32)
        FreeLibrary((HMODULE)lib_);
#else
        dlclose(lib_);
#endif
        lib_ = nullptr;
        vtable_ = nullptr;
    }
}

uint64_t NativePlugin::addCommandEntry(CsCommandFn fn, void* ud) {
    uint64_t id = nextCommandId_++;
    commands_[id] = CommandEntry{fn, ud};
    return id;
}

const NativePlugin::CommandEntry* NativePlugin::commandEntry(uint64_t id) const {
    auto it = commands_.find(id);
    return it == commands_.end() ? nullptr : &it->second;
}

uint64_t NativePlugin::scheduleTask(CsTaskFn fn, void* ud, int ticks, bool repeat) {
    auto idHolder = std::make_shared<uint64_t>(0);
    auto task = std::make_shared<scheduler::CallbackTask>([this, fn, ud, repeat, idHolder]() {
        auto it = tasks_.find(*idHolder);
        if (it == tasks_.end())
            return; // Cancelled between schedule and run.
        if (enabled_)
            invokeGuarded(fn, ud, name_);
        if (!repeat)
            tasks_.erase(*idHolder);
    });
    uint64_t id = scheduler::ServerScheduler::get().scheduleSync(task, ticks, repeat ? ticks : 0);
    *idHolder = id;
    tasks_[id] = {fn, ud};
    return id;
}

void NativePlugin::cancelTask(uint64_t id) {
    auto it = tasks_.find(id);
    if (it == tasks_.end())
        return;
    scheduler::ServerScheduler::get().cancel(id);
    tasks_.erase(it);
}

void NativePlugin::buildApiRoot() {
    apiRoot_.abiVersion = CS_PLUGIN_ABI_VERSION;
    apiRoot_.apiSize = (uint32_t)sizeof(CsPluginApi);
    apiRoot_.impl = this;
    apiRoot_.serverApi = CURSESTONE_API;
    apiRoot_.pluginName = name_.c_str();
    apiRoot_.pluginDir = pluginsDir_.c_str();
    apiRoot_.dataDir = dataDir_.c_str();
    apiRoot_.log = &g_logApi;
    apiRoot_.scheduler = &g_schedApi;
    apiRoot_.commands = &g_cmdApi;
    apiRoot_.events = &g_eventApi;
    apiRoot_.server = &g_serverApi;
    apiRoot_.player = &g_playerApi;
    apiRoot_.world = &g_worldApi;
    apiRoot_.data = &g_dataApi;
}

bool NativePlugin::ensureDataDir() {
    std::error_code ec;
    fs::create_directories(dataDir_, ec);
    if (ec) {
        log().error("[" + name_ + "] could not create data folder '" + dataDir_ + "': " +
                    ec.message());
        return false;
    }
    return true;
}

bool NativePlugin::openLibrary() {
#if defined(_WIN32)
    int wlen = MultiByteToWideChar(CP_UTF8, 0, libPath_.c_str(), -1, nullptr, 0);
    std::wstring wpath((size_t)wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, libPath_.c_str(), -1, wpath.data(), wlen);
    lib_ = (void*)LoadLibraryW(wpath.c_str());
    if (!lib_) {
        log().error("[" + name_ + "] LoadLibrary failed for '" + libPath_ + "' (error " +
                    std::to_string(GetLastError()) + ")");
        return false;
    }
    void* sym = (void*)GetProcAddress((HMODULE)lib_, CS_PLUGIN_ENTRY);
#else
    lib_ = dlopen(libPath_.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib_) {
        const char* dlerr = dlerror();
        log().error("[" + name_ + "] dlopen failed for '" + libPath_ + "': " +
                    (dlerr ? dlerr : "unknown error"));
        return false;
    }
    void* sym = dlsym(lib_, CS_PLUGIN_ENTRY);
#endif

    if (!sym) {
        log().error("[" + name_ + "] '" + stem_ +
                    "' does not export " + CS_PLUGIN_ENTRY +
                    " — it is not a CurseStone native plugin");
        return false;
    }
    using EntryFn = const CsPluginVTable* (*)(void);
    vtable_ = reinterpret_cast<EntryFn>(sym)();
    if (!vtable_ || vtable_->abiVersion != CS_PLUGIN_ABI_VERSION) {
        log().error("[" + name_ + "] '" + stem_ + "' was built against plugin ABI " +
                    std::to_string(vtable_ ? vtable_->abiVersion : 0) +
                    ", but this server provides ABI " + std::to_string(CS_PLUGIN_ABI_VERSION) +
                    " — rebuild the plugin");
        vtable_ = nullptr;
        return false;
    }
    return true;
}

bool NativePlugin::load() {
    if (!ensureDataDir())
        return false;
    if (!openLibrary())
        return false;

    // The descriptor IS the manifest: no plugin.yml anywhere on this path.
    const CsPluginDesc* desc = vtable_->desc ? vtable_->desc() : nullptr;
    if (!desc || !desc->name || !*desc->name) {
        if (desc)
            log().warn("[" + name_ + "] descriptor carries no name — using the file stem '" +
                       stem_ + "'");
        else
            log().error("[" + name_ + "] " + stem_ + ": vtable::desc() returned no descriptor");
        if (!desc)
            return false;
    } else {
        name_ = desc->name;
    }
    if (desc->version && *desc->version)
        version_ = desc->version;
    if (desc->author && *desc->author)
        author_ = desc->author;

    // The descriptor may pin the server API it was built for. Empty means
    // "built against this SDK"; an incompatible claim discards the plugin.
    const char* apiClaim = desc->api && *desc->api ? desc->api : "";
    if (*apiClaim) {
        bool exact = false;
        if (!apiCompatible(apiClaim, &exact)) {
            log().error("[" + name_ + "] was built for plugin API " + apiClaim +
                        ", but this server provides API " + CURSESTONE_API + " — plugin discarded");
            return false;
        }
        if (!exact)
            log().warn("[" + name_ + "] declares plugin API " + apiClaim +
                       " while the server provides " + CURSESTONE_API +
                       " (compatible, but not exact)");
    }

    buildApiRoot();

    // onLoad is required and its failure discards the plugin before any
    // other hook runs.
    int rc = 0;
    if (vtable_->onLoad) {
        try {
            rc = vtable_->onLoad(&apiRoot_);
        } catch (const std::exception& e) {
            log().error("[" + name_ + "] onLoad threw an exception: " + e.what());
            return false;
        } catch (...) {
            log().error("[" + name_ + "] onLoad threw an unknown exception");
            return false;
        }
    }
    if (rc != 0) {
        log().error("[" + name_ + "] onLoad returned " + std::to_string(rc) +
                    " — plugin discarded");
        return false;
    }
    loadedOk_ = true;
    return true;
}

bool NativePlugin::enable() {
    if (enabled_)
        return true;
    enabled_ = true;

    if (vtable_ && vtable_->onEnable) {
        std::string pluginName = name_;
        try {
            vtable_->onEnable();
        } catch (const std::exception& e) {
            log().error("[" + pluginName + "] onEnable threw an exception: " + e.what());
            disable();
            return false;
        } catch (...) {
            log().error("[" + pluginName + "] onEnable threw an unknown exception");
            disable();
            return false;
        }
    }

    std::string line = "Plugin enabled: " + name_ + " v" + version_ + " [native]";
    if (!author_.empty())
        line += " by " + author_;
    log().info(line);
    return true;
}

void NativePlugin::disable() {
    if (!enabled_)
        return;
    enabled_ = false;

    if (vtable_ && vtable_->onDisable) {
        std::string pluginName = name_;
        try {
            vtable_->onDisable();
        } catch (const std::exception& e) {
            log().error("[" + pluginName + "] onDisable threw an exception: " + e.what());
        } catch (...) {
            log().error("[" + pluginName + "] onDisable threw an unknown exception");
        }
    }

    // Same detach order as LuaPlugin: listeners first, then tasks.
    event::EventManager::get().unlistenAll(this);
    for (const auto& [id, task] : tasks_)
        scheduler::ServerScheduler::get().cancel(id);
    tasks_.clear();

    log().info("Plugin disabled: " + name_);
}

} // namespace cursestone::plugin
