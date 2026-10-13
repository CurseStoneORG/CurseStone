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
// BindingsEvents — Fase 6: API de eventos para los plugins Lua.
//
//   events.on("PlayerJoin", function(e) ... end)
//   events.on("PlayerQuit", function(e) ... end)
//   events.on("PlayerChat", function(e) ... end)
//   events.on("BlockBreak", function(e) ... end)
//   events.on("BlockPlace", function(e) ... end)
//
// `e` es una tabla simple por invocación:
//   - eventos de jugador: e.player (NOMBRE, string), e.message
//   - eventos de bloque:  e.player, e.x, e.y, e.z, e.block_id
//   - e.cancel() cancela el evento (suprime mensaje / bloquea la acción)
//
// Prioridad fija NORMAL, ignoreCancelled=false. El owner del listener es
// el propio LuaPlugin, así que disable() -> unlistenAll(this) los quita
// todos sin dejar callbacks colgantes.
//
// TODO corre en el hilo principal: los eventos de juego se disparan desde
// el tick loop y el patrón respeta la pila exactamente (base = gettop al
// entrar, settop(base) al salir, pase lo que pase).
#include "LuaBindings.h"
#include "LuaPlugin.h"
#include "Player.h"                     // definición completa de Player (getName)
#include "event/EventManager.h"
#include "event/player/PlayerJoinEvent.h"
#include "event/player/PlayerQuitEvent.h"
#include "event/player/PlayerChatEvent.h"
#include "event/block/BlockBreakEvent.h"
#include "event/block/BlockPlaceEvent.h"
#include <functional>
#include <string>

namespace cursestone::plugin {

namespace {

// Cierre C para e.cancel(): marca e.cancelled = true en la propia tabla e
// (guardada como upvalue). El wrapper lee `cancelled` DESPUÉS del pcall.
int l_cancel(lua_State* L) {
    lua_pushboolean(L, 1);
    lua_setfield(L, lua_upvalueindex(1), "cancelled");
    return 0;
}

// Patrón genérico que se repite para los 5 eventos:
//   1. construir la tabla e (con e.cancel como closure),
//   2. llenar los campos con `fill`,
//   3. llamar al handler Lua con pcall,
//   4. leer de vuelta `cancelled` y los campos escribibles con readBack,
//   5. restaurar la pila exactamente como estaba.
template <typename EventT>
void hook(LuaPlugin* pl, int fnRef,
          std::function<void(lua_State*, int /*eIdx*/, EventT&)> fill,
          std::function<void(lua_State*, int /*eIdx*/, EventT&)> readBack) {
    event::EventManager::get().listen<EventT>(pl,   // owner = el plugin (unlistenAll(this) lo quita)
        [pl, fnRef, fill, readBack](EventT& ev) {
            if (!pl->isEnabled()) return;
            lua_State* L = pl->L();
            int base = lua_gettop(L);

            lua_newtable(L);                        // e
            int eIdx = lua_gettop(L);

            // e.cancel: closure con 1 upvalue (la propia tabla e).
            lua_pushvalue(L, eIdx);
            lua_pushcclosure(L, l_cancel, 1);
            lua_setfield(L, eIdx, "cancel");

            fill(L, eIdx, ev);

            // Primero la función, luego el argumento (el orden importa).
            lua_rawgeti(L, LUA_REGISTRYINDEX, fnRef);
            lua_pushvalue(L, eIdx);
            if (pl->pcall(1, 0)) {
                // Leer de vuelta: ¿cancelaron el evento?
                lua_getfield(L, eIdx, "cancelled");
                if (lua_toboolean(L, -1)) ev.setCancelled(true);
                lua_pop(L, 1);
                readBack(L, eIdx, ev);
            }
            lua_settop(L, base);                    // la pila queda como estaba
        });
}

// fill común de los eventos de jugador: e.player es el NOMBRE (string).
// Si ev.player es null, no explotar: string vacío.
template <typename EventT>
void fillPlayer(lua_State* L, int eIdx, EventT& ev) {
    lua_pushstring(L, ev.player ? ev.player->getName().c_str() : "");
    lua_setfield(L, eIdx, "player");
}

// readBack de Join/Quit/Chat: si e.message es string, reescribir el mensaje.
template <typename EventT>
void readBackMessage(lua_State* L, int eIdx, EventT& ev) {
    lua_getfield(L, eIdx, "message");
    if (const char* s = lua_tostring(L, -1)) ev.message = s;
    lua_pop(L, 1);
}

// fill común de los eventos de bloque: posición + id de bloque.
template <typename EventT>
void fillBlock(lua_State* L, int eIdx, EventT& ev) {
    lua_pushstring(L, ev.player ? ev.player->getName().c_str() : "");
    lua_setfield(L, eIdx, "player");
    lua_pushnumber(L, (lua_Number)ev.pos.x);
    lua_setfield(L, eIdx, "x");
    lua_pushnumber(L, (lua_Number)ev.pos.y);
    lua_setfield(L, eIdx, "y");
    lua_pushnumber(L, (lua_Number)ev.pos.z);
    lua_setfield(L, eIdx, "z");
    lua_pushinteger(L, (lua_Integer)ev.block_id);
    lua_setfield(L, eIdx, "block_id");
}

// events.on(nombre, fn)
int l_events_on(lua_State* L) {
    LuaPlugin* pl = LuaPlugin::from(L);
    std::string name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    if      (name == "PlayerJoin") {
        hook<event::player::PlayerJoinEvent>(pl, ref, fillPlayer<event::player::PlayerJoinEvent>,
                                             readBackMessage<event::player::PlayerJoinEvent>);
    }
    else if (name == "PlayerQuit") {
        hook<event::player::PlayerQuitEvent>(pl, ref, fillPlayer<event::player::PlayerQuitEvent>,
                                             readBackMessage<event::player::PlayerQuitEvent>);
    }
    else if (name == "PlayerChat") {
        hook<event::player::PlayerChatEvent>(pl, ref, fillPlayer<event::player::PlayerChatEvent>,
                                             readBackMessage<event::player::PlayerChatEvent>);
    }
    else if (name == "BlockBreak") {
        auto f = [](lua_State* L, int eIdx, event::block::BlockBreakEvent& ev) { fillBlock(L, eIdx, ev); };
        hook<event::block::BlockBreakEvent>(pl, ref, f, {});
    }
    else if (name == "BlockPlace") {
        auto f = [](lua_State* L, int eIdx, event::block::BlockPlaceEvent& ev) { fillBlock(L, eIdx, ev); };
        hook<event::block::BlockPlaceEvent>(pl, ref, f, {});
    }
    else {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "unknown event: %s", name.c_str());
    }
    return 0;
}

} // namespace

void bindEvents(lua_State* L) {
    static const luaL_Reg evF[] = {
        {"on", l_events_on},
        {nullptr, nullptr}};
    lua_newtable(L);
    luaL_setfuncs(L, evF, 0);
    lua_setglobal(L, "events");
}

} // namespace cursestone::plugin
