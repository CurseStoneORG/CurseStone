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
// BindingsCore — API core expuesta to the plugins Lua.
//
//   log.info/warn/error(msg)          antepone "[NombrePlugin] "
//   server.broadcast(msg)             message to all the players
//   server.players()                  list of names connected
//   player.message(name, msg)         message to a player (false if not exists)
//   player.tip(name, msg)             TIP flotante (false if not exists)
//   player.popup(name, msg)           POPUP about the hotbar (false if not exists)
//   player.kick(name, reason)         expulsa (false if not exists)
//   player.teleport(name, x, and, z)    teletransporta (false if not exists)
//   player.teleportWorld(name, world, // teletransporta to other world
//                        x, and, z)     (false if not exist)
//   player.getWorld(name)             handle world of the player (nil if not)
//   player.isOp(name)                 true if is op (false if not exists)
//   player.giveItem(name, id, count, meta, customName)
//                                     añade un stack al inventario;
//                                     customName (opcional) = nombre
//                                     visible del item (§ soportado)
//   player.setNameTag(name, tag)      nametag sobre la cabeza + pestaña
//                                     del servidor (códigos § = rangos)
//   player.getNameTag(name)           nametag actual (nil si no existe)
//   player.setDisplayName(name, dn)   nombre SOLO para el chat
//   player.getDisplayName(name)       display name actual (nil si no)
//   player.setPermission(name, node, v) concede/niega a node of permiso
//                                     in memory (false if not exists the player)
//   player.hasPermission(name, node)  ¿has that node? (false if not exists)
//   scheduler.later(ticks, fn)        runs fn a time; returns id
//   scheduler.every(ticks, fn)        runs fn each ticks; returns id
//   scheduler.cancel(id)              cancela a tarea
//   plugin.api                        version of the API of the server ("1.0.0")
//   plugin.name / .author / .version  metadata of this plugin
//   import("core/Example")            function global: load a module Lua
//                                     of the own plugin (looks for in src/,
//                                     extension optional, with cache)
//   plugin.include("src/x.lua")       variante explicit of import (path
//                                     exact relativa to its folder, with cache)
//
// REGLAS:
//   - everything by name of player (string). never is saves a player*:
//     is looks for with getPlayerExact in each call.
//   - the scheduler runs in the thread main; the functions Lua is
//     are stored as refs in the registry of the state of the plugin.
#include "LuaBindings.h"
#include "LuaPlugin.h"
#include "Server.h"
#include "Player.h"
#include "inventory/PlayerInventory.h"
#include "item/ItemNbt.h"
#include "utils/MainLogger.hpp"
#include "scheduler/ServerScheduler.h"
#include "scheduler/CallbackTask.h"
#include <memory>

namespace cursestone::plugin {

namespace {

// Helper for register a tabla of functions as global.
void setTable(lua_State* L, const char* name, const luaL_Reg* funcs) {
    lua_newtable(L);
    luaL_setfuncs(L, funcs, 0);
    lua_setglobal(L, name);
}

// ---------- log ----------
int logWith(lua_State* L, int level) {
    LuaPlugin* pl = LuaPlugin::from(L);
    std::string msg = "[" + pl->name() + "] " + luaL_checkstring(L, 1);
    auto& lg = utils::MainLogger::get();
    if (level == 0) lg.info(msg);
    else if (level == 1) lg.warn(msg);
    else lg.error(msg);
    return 0;
}
int l_log_info(lua_State* L)  { return logWith(L, 0); }
int l_log_warn(lua_State* L)  { return logWith(L, 1); }
int l_log_error(lua_State* L) { return logWith(L, 2); }

// ---------- server ----------
int l_server_broadcast(lua_State* L) {
    Server::getInstance().broadcastMessage(luaL_checkstring(L, 1));
    return 0;
}
int l_server_players(lua_State* L) {
    auto list = Server::getInstance().playersSnapshot();
    lua_createtable(L, (int)list.size(), 0);
    int i = 1;
    for (auto& p : list) {
        lua_pushstring(L, p->getName().c_str());
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// ---------- player (pattern: look for by name in each call) ----------
Player* findPlayer(lua_State* L, int idx) {
    return Server::getInstance().getPlayerExact(luaL_checkstring(L, idx));
}
int l_player_message(lua_State* L) {
    Player* p = findPlayer(L, 1);
    const char* msg = luaL_checkstring(L, 2);
    if (!p) { lua_pushboolean(L, 0); return 1; }
    p->sendMessage(msg);
    lua_pushboolean(L, 1);
    return 1;
}
int l_player_tip(lua_State* L) {
    Player* p = findPlayer(L, 1);
    const char* msg = luaL_checkstring(L, 2);
    if (!p) { lua_pushboolean(L, 0); return 1; }
    p->sendTip(msg);
    lua_pushboolean(L, 1);
    return 1;
}
int l_player_popup(lua_State* L) {
    Player* p = findPlayer(L, 1);
    const char* msg = luaL_checkstring(L, 2);
    if (!p) { lua_pushboolean(L, 0); return 1; }
    p->sendPopup(msg);
    lua_pushboolean(L, 1);
    return 1;
}
int l_player_kick(lua_State* L) {
    Player* p = findPlayer(L, 1);
    const char* reason = luaL_optstring(L, 2, "Expulsado");
    if (!p) { lua_pushboolean(L, 0); return 1; }
    p->kick(reason);
    lua_pushboolean(L, 1);
    return 1;
}
int l_player_teleport(lua_State* L) {
    Player* p = findPlayer(L, 1);
    lua_Number x = luaL_checknumber(L, 2);
    lua_Number y = luaL_checknumber(L, 3);
    lua_Number z = luaL_checknumber(L, 4);
    if (!p) { lua_pushboolean(L, 0); return 1; }
    p->teleport((double)x, (double)y, (double)z);
    lua_pushboolean(L, 1);
    return 1;
}
// teleportWorld(name, world, x, and, z): just like teleport() but about
// other world; if the dimension changes the client receives
// ChangeDimensionPacket and the chunks of the target.
int l_player_teleportWorld(lua_State* L) {
    Player* p = findPlayer(L, 1);
    const char* worldName = luaL_checkstring(L, 2);
    lua_Number x = luaL_checknumber(L, 3);
    lua_Number y = luaL_checknumber(L, 4);
    lua_Number z = luaL_checknumber(L, 5);
    if (!p) { lua_pushboolean(L, 0); return 1; }
    World* target = Server::getInstance().getWorldByName(worldName);
    if (!target) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, p->switchWorld(target, (double)x, (double)y, (double)z,
                                      false) ? 1 : 0);
    return 1;
}
// getWorld(name): handle `world` of the world in that which is the player.
int l_player_getWorld(lua_State* L) {
    Player* p = findPlayer(L, 1);
    if (!p) { lua_pushnil(L); return 1; }
    pushWorldHandle(L, p->getWorld());
    return 1;
}
int l_player_isOp(lua_State* L) {
    Player* p = findPlayer(L, 1);
    lua_pushboolean(L, p ? p->isOp() : 0);
    return 1;
}
int l_player_setPermission(lua_State* L) {
    Player* p = findPlayer(L, 1);
    const char* node = luaL_checkstring(L, 2);
    bool value = lua_toboolean(L, 3) != 0;
    if (!p) { lua_pushboolean(L, 0); return 1; }
    p->setPermission(node, value);       // PermissibleBase (in memory)
    lua_pushboolean(L, 1);
    return 1;
}
int l_player_hasPermission(lua_State* L) {
    Player* p = findPlayer(L, 1);
    const char* node = luaL_checkstring(L, 2);
    lua_pushboolean(L, p ? p->hasPermission(node) : 0);
    return 1;
}

// ---------- items + names (API 1.0.1 — ranks / custom items) ----------
// giveItem(name, id, count, meta, customName): adds an item stack to the
// player's inventory. count defaults to 1, meta to 0. `customName` is the
// VISIBLE item name the 0.15.10 client renders (anvil-style); it rides
// as the {display:{Name}} NBT compound of the stack, so it survives
// inventory moves, dropping and picking back up. Returns false when the
// player does not exist or the stack did not fit.
int l_player_giveItem(lua_State* L) {
    Player* p = findPlayer(L, 1);
    lua_Integer id    = luaL_checkinteger(L, 2);
    lua_Integer count = luaL_optinteger(L, 3, 1);
    lua_Integer meta  = luaL_optinteger(L, 4, 0);
    const char* customName = luaL_optstring(L, 5, nullptr);
    if (!p || !p->getInventory()) { lua_pushboolean(L, 0); return 1; }
    if (id <= 0 || id > 65535 || count < 1) { lua_pushboolean(L, 0); return 1; }
    if (count > 2304) count = 2304;   // 36 slots * 64 — keep it sane

    item::ItemStack st;
    st.id    = (uint16_t)id;
    st.count = (int)count;
    st.meta  = (uint8_t)(meta & 0xFF);
    if (customName && *customName) st.nbt = item::buildDisplayNbt(customName);

    auto leftovers = p->getInventory()->addItem({st});
    bool fit = leftovers.empty() || leftovers[0].isEmpty() ||
               leftovers[0].count < count;
    p->sendInventory();   // full resync — the client renders the new stack
    lua_pushboolean(L, fit ? 1 : 0);
    return 1;
}
// setNameTag(name, tag): the name ABOVE THE HEAD and in the server list
// tab (PMMP Player::setNameTag). Accepts §-color codes — this is the
// rank-prefix surface. Re-broadcasts REMOVE+ADD PlayerListPacket to
// everyone. resetNameTag == setNameTag(name, playerName).
int l_player_setNameTag(lua_State* L) {
    Player* p = findPlayer(L, 1);
    const char* tag = luaL_checkstring(L, 2);
    if (!p) { lua_pushboolean(L, 0); return 1; }
    p->setNameTag(tag);
    lua_pushboolean(L, 1);
    return 1;
}
int l_player_getNameTag(lua_State* L) {
    Player* p = findPlayer(L, 1);
    if (!p) { lua_pushnil(L); return 1; }
    lua_pushstring(L, p->getNameTag().c_str());
    return 1;
}
// setDisplayName(name, dn): chat-only alias (PMMP Player::setDisplayName);
// the chat line uses it, the tag above the head does not.
int l_player_setDisplayName(lua_State* L) {
    Player* p = findPlayer(L, 1);
    const char* dn = luaL_checkstring(L, 2);
    if (!p) { lua_pushboolean(L, 0); return 1; }
    p->setDisplayName(dn);
    lua_pushboolean(L, 1);
    return 1;
}
int l_player_getDisplayName(lua_State* L) {
    Player* p = findPlayer(L, 1);
    if (!p) { lua_pushnil(L); return 1; }
    lua_pushstring(L, p->getDisplayName().c_str());
    return 1;
}

// ---------- scheduler ----------
// common to later() and every(): save the function as ref, schedule a
// CallbackTask in the ServerScheduler and return the id of tarea to Lua.
int scheduleImpl(lua_State* L, bool repeat) {
    LuaPlugin* pl = LuaPlugin::from(L);
    int ticks = (int)luaL_checkinteger(L, 1);
    if (ticks < 1) ticks = 1;
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);          // save the function
    auto idHolder = std::make_shared<uint64_t>(0);     // the id is is known after schedule

    auto task = std::make_shared<scheduler::CallbackTask>(
        [pl, ref, repeat, idHolder]() {
            if (!pl->isEnabled()) return;              // plugin already deshabilitado
            lua_rawgeti(pl->L(), LUA_REGISTRYINDEX, ref);
            pl->pcall(0, 0);
            if (!repeat) pl->releaseTask(*idHolder);   // one-shot: soltar the ref
        });

    uint64_t id = scheduler::ServerScheduler::get().scheduleSync(task, ticks, repeat ? ticks : 0);
    *idHolder = id;
    pl->addTask(id, ref);
    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}
int l_scheduler_later(lua_State* L)  { return scheduleImpl(L, false); }
int l_scheduler_every(lua_State* L)  { return scheduleImpl(L, true); }
int l_scheduler_cancel(lua_State* L) {
    LuaPlugin::from(L)->cancelTask((uint64_t)luaL_checkinteger(L, 1));
    return 0;
}

// ---------- plugin (metadata + load of files of the plugin) ----------
// plugin.include(path) -> delegates in LuaPlugin::include (validation of path
// safe relativa to the folder of the plugin + cache tipo require).
int l_plugin_include(lua_State* L) {
    return LuaPlugin::from(L)->include(L);
}
// import(path) -> delegates in LuaPlugin::import (looks for in src/, extension
// optional, same cache). is registers as function global `import`.
int l_plugin_import(lua_State* L) {
    return LuaPlugin::from(L)->import(L);
}

// Tabla `plugin`: campos fijos (api/name/author/version) + include().
void bindPluginTable(lua_State* L) {
    LuaPlugin* pl = LuaPlugin::from(L);
    lua_newtable(L);

    lua_pushstring(L, PLUGIN_API_VERSION);          // plugin.api
    lua_setfield(L, -2, "api");
    lua_pushstring(L, pl->name().c_str());          // plugin.name
    lua_setfield(L, -2, "name");
    lua_pushstring(L, pl->author().c_str());        // plugin.author
    lua_setfield(L, -2, "author");
    lua_pushstring(L, pl->version().c_str());       // plugin.version
    lua_setfield(L, -2, "version");

    static const luaL_Reg pluginF[] = {
        {"include", l_plugin_include},
        {nullptr, nullptr}};
    luaL_setfuncs(L, pluginF, 0);

    lua_setglobal(L, "plugin");
}

} // namespace

void bindCore(lua_State* L) {
    static const luaL_Reg logF[] = {
        {"info", l_log_info}, {"warn", l_log_warn}, {"error", l_log_error},
        {nullptr, nullptr}};
    static const luaL_Reg srvF[] = {
        {"broadcast", l_server_broadcast}, {"players", l_server_players},
        {nullptr, nullptr}};
    static const luaL_Reg plrF[] = {
        {"message", l_player_message}, {"tip", l_player_tip},
        {"popup", l_player_popup}, {"kick", l_player_kick},
        {"teleport", l_player_teleport}, {"teleportWorld", l_player_teleportWorld},
        {"getWorld", l_player_getWorld}, {"isOp", l_player_isOp},
        {"setPermission", l_player_setPermission},
        {"hasPermission", l_player_hasPermission},
        {"giveItem", l_player_giveItem},
        {"setNameTag", l_player_setNameTag}, {"getNameTag", l_player_getNameTag},
        {"setDisplayName", l_player_setDisplayName},
        {"getDisplayName", l_player_getDisplayName},
        {nullptr, nullptr}};
    static const luaL_Reg schF[] = {
        {"later", l_scheduler_later}, {"every", l_scheduler_every},
        {"cancel", l_scheduler_cancel},
        {nullptr, nullptr}};
    setTable(L, "log", logF);
    setTable(L, "server", srvF);
    setTable(L, "player", plrF);
    setTable(L, "scheduler", schF);
    bindPluginTable(L);   // plugin.api / .name / .author / .version / .include

    // import("module"): load of módulos Lua of the own plugin (the way
    // recommended; looks for in src/ and not requires extension). global, as requires
    // the flujo natural: local Example = import("core/Example").
    lua_pushcfunction(L, l_plugin_import);
    lua_setglobal(L, "import");
}

} // namespace cursestone::plugin
