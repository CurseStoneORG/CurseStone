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
// BindingsCore — API core expuesta a los plugins Lua.
//
//   log.info/warn/error(msg)          antepone "[NombrePlugin] "
//   server.broadcast(msg)             mensaje a todos los jugadores
//   server.players()                  lista de nombres conectados
//   player.message(name, msg)         mensaje a un jugador (false si no existe)
//   player.tip(name, msg)             TIP flotante (false si no existe)
//   player.popup(name, msg)           POPUP sobre el hotbar (false si no existe)
//   player.kick(name, reason)         expulsa (false si no existe)
//   player.teleport(name, x, y, z)    teletransporta (false si no existe)
//   player.isOp(name)                 true si es op (false si no existe)
//   player.setPermission(name, nodo, v) concede/niega un nodo de permiso
//                                     EN MEMORIA (false si no existe el jugador)
//   player.hasPermission(name, nodo)  ¿tiene ese nodo? (false si no existe)
//   scheduler.later(ticks, fn)        corre fn una vez; devuelve id
//   scheduler.every(ticks, fn)        corre fn cada ticks; devuelve id
//   scheduler.cancel(id)              cancela una tarea
//   plugin.api                        versión de la API del servidor ("1.0.0")
//   plugin.name / .author / .version  metadatos de ESTE plugin
//   import("core/Example")            FUNCIÓN GLOBAL: carga un módulo Lua
//                                     del propio plugin (busca en src/,
//                                     extensión opcional, con caché)
//   plugin.include("src/x.lua")       variante explícita de import (ruta
//                                     exacta relativa a su carpeta, con caché)
//
// REGLAS:
//   - Todo por NOMBRE de jugador (string). Nunca se guarda un Player*:
//     se busca con getPlayerExact en CADA llamada.
//   - El scheduler corre en el hilo principal; las funciones Lua se
//     guardan como refs en el registry del state del plugin.
#include "LuaBindings.h"
#include "LuaPlugin.h"
#include "Server.h"
#include "Player.h"
#include "utils/MainLogger.hpp"
#include "scheduler/ServerScheduler.h"
#include "scheduler/CallbackTask.h"
#include <memory>

namespace cursestone::plugin {

namespace {

// Helper para registrar una tabla de funciones como global.
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

// ---------- player (patrón: buscar por nombre en CADA llamada) ----------
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
    p->setPermission(node, value);       // PermissibleBase (en memoria)
    lua_pushboolean(L, 1);
    return 1;
}
int l_player_hasPermission(lua_State* L) {
    Player* p = findPlayer(L, 1);
    const char* node = luaL_checkstring(L, 2);
    lua_pushboolean(L, p ? p->hasPermission(node) : 0);
    return 1;
}

// ---------- scheduler ----------
// Común a later() y every(): guardar la función como ref, programar una
// CallbackTask en el ServerScheduler y devolver el id de tarea a Lua.
int scheduleImpl(lua_State* L, bool repeat) {
    LuaPlugin* pl = LuaPlugin::from(L);
    int ticks = (int)luaL_checkinteger(L, 1);
    if (ticks < 1) ticks = 1;
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);          // guardar la función
    auto idHolder = std::make_shared<uint64_t>(0);     // el id se conoce después de programar

    auto task = std::make_shared<scheduler::CallbackTask>(
        [pl, ref, repeat, idHolder]() {
            if (!pl->isEnabled()) return;              // plugin ya deshabilitado
            lua_rawgeti(pl->L(), LUA_REGISTRYINDEX, ref);
            pl->pcall(0, 0);
            if (!repeat) pl->releaseTask(*idHolder);   // one-shot: soltar la ref
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

// ---------- plugin (metadatos + carga de archivos del plugin) ----------
// plugin.include(ruta) -> delega en LuaPlugin::include (validación de ruta
// segura relativa a la carpeta del plugin + caché tipo require).
int l_plugin_include(lua_State* L) {
    return LuaPlugin::from(L)->include(L);
}
// import(ruta) -> delega en LuaPlugin::import (busca en src/, extensión
// opcional, misma caché). Se registra como FUNCIÓN GLOBAL `import`.
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
        {"teleport", l_player_teleport}, {"isOp", l_player_isOp},
        {"setPermission", l_player_setPermission},
        {"hasPermission", l_player_hasPermission},
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

    // import("modulo"): carga de módulos Lua del propio plugin (la forma
    // recomendada; busca en src/ y no exige extensión). Global, como pide
    // el flujo natural: local Example = import("core/Example").
    lua_pushcfunction(L, l_plugin_import);
    lua_setglobal(L, "import");
}

} // namespace cursestone::plugin
