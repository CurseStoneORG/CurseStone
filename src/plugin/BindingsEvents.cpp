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
//   events.on("EntityDamage", function(e) ... end)          -- dano (cualquier causa)
//   events.on("EntityDamageByEntity", function(e) ... end)  -- PvP: golpe/flecha
//   events.on("PlayerDeath", function(e) ... end)
//   events.on("PlayerRespawn", function(e) ... end)
//
// `e` es una tabla simple por invocación:
//   - eventos de jugador: e.player (NOMBRE, string), e.message
//   - eventos de bloque:  e.player, e.x, e.y, e.z, e.block_id
//   - eventos de dano:    e.entity (víctima), e.amount (ESCRIBIBLE — la
//     cantidad leída de vuelta se aplica), e.cause (string: "entity_attack",
//     "projectile", "fall", ...), y en ByEntity además e.damager (el
//     atacante). e.cancel() anula el dano por completo.
//
// Prioridad fija NORMAL. El owner del listener es el propio LuaPlugin,
// así que disable() -> unlistenAll(this) los quita todos sin dejar
// callbacks colgantes.
#include "LuaBindings.h"
#include "LuaPlugin.h"
#include "Player.h"                     // definición completa de Player (getName)
#include "event/EventManager.h"
#include "event/player/PlayerJoinEvent.h"
#include "event/player/PlayerQuitEvent.h"
#include "event/player/PlayerChatEvent.h"
#include "event/player/PlayerDeathEvent.h"
#include "event/player/PlayerRespawnEvent.h"
#include "event/block/BlockBreakEvent.h"
#include "event/block/BlockPlaceEvent.h"
#include "event/entity/EntityDamageEvent.h"
#include "event/entity/EntityDamageByEntityEvent.h"
#include <functional>
#include <string>
#include <type_traits>

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
                // Leer de vuelta: ¿cancelaron el evento? Solo los eventos
                // Cancellable (if constexpr: PlayerRespawnEvent, p.ej.,
                // NO es cancelable a propósito y no tiene setCancelled).
                lua_getfield(L, eIdx, "cancelled");
                if (lua_toboolean(L, -1)) {
                    if constexpr (std::is_base_of_v<event::Cancellable, EventT>) {
                        ev.setCancelled(true);
                    }
                }
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

// ---- helpers de los eventos de daño ----

// Nombre legible de una entidad para los plugins: los jugadores (el único
// tipo de entidad dañable hoy) por su nombre; cualquier otra cosa como
// "eid:<n>". Nunca nil, para que el handler pueda comparar sin tocarse.
void fillEntityField(lua_State* L, int eIdx, const char* field,
                     cursestone::entity::Entity* e) {
    if (!e) { lua_pushstring(L, "unknown"); lua_setfield(L, eIdx, field); return; }
    if (auto* pl = dynamic_cast<Player*>(e)) {
        lua_pushstring(L, pl->getName().c_str());
    } else {
        lua_pushfstring(L, "eid:%lld", (long long)e->runtimeId());
    }
    lua_setfield(L, eIdx, field);
}

// DamageCause -> string PMMP-style (misma familia de nombres que los
// eventos de PocketMine/Nukkit, minúsculas con guion bajo).
const char* damageCauseName(event::entity::DamageCause c) {
    using event::entity::DamageCause;
    switch (c) {
        case DamageCause::ENTITY_ATTACK: return "entity_attack";
        case DamageCause::CONTACT:       return "contact";
        case DamageCause::FALL:          return "fall";
        case DamageCause::LAVA:          return "lava";
        case DamageCause::FIRE:          return "fire";
        case DamageCause::FIRE_TICK:     return "fire_tick";
        case DamageCause::DROWNING:      return "drowning";
        case DamageCause::CACTUS:        return "cactus";
        case DamageCause::STARVATION:    return "starvation";
        case DamageCause::VOID:          return "void";
        case DamageCause::PROJECTILE:    return "projectile";
        case DamageCause::CUSTOM:        return "custom";
    }
    return "custom";
}

// e.entity (víctima) + e.amount (escribible) + e.cause.
void fillDamage(lua_State* L, int eIdx, event::entity::EntityDamageEvent& ev) {
    fillEntityField(L, eIdx, "entity", ev.entity);
    lua_pushnumber(L, (lua_Number)ev.amount);
    lua_setfield(L, eIdx, "amount");
    lua_pushstring(L, damageCauseName(ev.cause));
    lua_setfield(L, eIdx, "cause");
}

// Leer de vuelta e.amount: el plugin puede reescribir la cantidad (para
// armadura, multiplicadores de PvP, zonas sin dano, etc.). Se satura a
// >= 0 — el pipeline ya satura la salud a 0, así que números negativos
// no tienen sentido y jamás deben llegar al cable.
void readBackAmount(lua_State* L, int eIdx, event::entity::EntityDamageEvent& ev) {
    lua_getfield(L, eIdx, "amount");
    if (lua_isnumber(L, -1)) {
        lua_Number a = lua_tonumber(L, -1);
        ev.amount = (float)(a > 0 ? a : 0);
    }
    lua_pop(L, 1);
}

// EntityDamageByEntity: igual + e.damager (atacante).
void fillDamageByEntity(lua_State* L, int eIdx,
                        event::entity::EntityDamageByEntityEvent& ev) {
    fillDamage(L, eIdx, ev);
    fillEntityField(L, eIdx, "damager", ev.damager);
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
    else if (name == "EntityDamage") {
        auto f = [](lua_State* L, int eIdx, event::entity::EntityDamageEvent& ev) {
            fillDamage(L, eIdx, ev);
        };
        hook<event::entity::EntityDamageEvent>(pl, ref, f, readBackAmount);
    }
    else if (name == "EntityDamageByEntity") {
        auto f = [](lua_State* L, int eIdx, event::entity::EntityDamageByEntityEvent& ev) {
            fillDamageByEntity(L, eIdx, ev);
        };
        // Same read-back: e.amount is writable, e.cancel() voids the hit.
        hook<event::entity::EntityDamageByEntityEvent>(
            pl, ref, f,
            std::function<void(lua_State*, int, event::entity::EntityDamageByEntityEvent&)>(
                [](lua_State* L, int eIdx, event::entity::EntityDamageByEntityEvent& ev) {
                    readBackAmount(L, eIdx, ev);
                }));
    }
    else if (name == "PlayerDeath") {
        auto f = [](lua_State* L, int eIdx, event::player::PlayerDeathEvent& ev) {
            fillPlayer<event::player::PlayerDeathEvent>(L, eIdx, ev);
            lua_pushstring(L, ev.message.c_str());
            lua_setfield(L, eIdx, "message");
        };
        hook<event::player::PlayerDeathEvent>(pl, ref, f,
                                              readBackMessage<event::player::PlayerDeathEvent>);
    }
    else if (name == "PlayerRespawn") {
        auto f = [](lua_State* L, int eIdx, event::player::PlayerRespawnEvent& ev) {
            fillPlayer<event::player::PlayerRespawnEvent>(L, eIdx, ev);
            lua_pushnumber(L, (lua_Number)ev.respawnX);
            lua_setfield(L, eIdx, "x");
            lua_pushnumber(L, (lua_Number)ev.respawnY);
            lua_setfield(L, eIdx, "y");
            lua_pushnumber(L, (lua_Number)ev.respawnZ);
            lua_setfield(L, eIdx, "z");
        };
        // Not cancellable on purpose (a cancelled respawn would strand the
        // player dead): listeners may only observe.
        hook<event::player::PlayerRespawnEvent>(pl, ref, f, {});
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

    // events.CAUSE: constants of damage cause (same values as the
    // server enum; the strings of e.cause are the lowercase versions).
    lua_createtable(L, 0, 12);
    int ci = lua_gettop(L);
    auto setCause = [L, ci](const char* n, event::entity::DamageCause c) {
        lua_pushinteger(L, (lua_Integer)c);
        lua_setfield(L, ci, n);
    };
    setCause("CUSTOM", event::entity::DamageCause::CUSTOM);
    setCause("ENTITY_ATTACK", event::entity::DamageCause::ENTITY_ATTACK);
    setCause("CONTACT", event::entity::DamageCause::CONTACT);
    setCause("FALL", event::entity::DamageCause::FALL);
    setCause("LAVA", event::entity::DamageCause::LAVA);
    setCause("FIRE", event::entity::DamageCause::FIRE);
    setCause("FIRE_TICK", event::entity::DamageCause::FIRE_TICK);
    setCause("DROWNING", event::entity::DamageCause::DROWNING);
    setCause("CACTUS", event::entity::DamageCause::CACTUS);
    setCause("STARVATION", event::entity::DamageCause::STARVATION);
    setCause("VOID", event::entity::DamageCause::VOID);
    setCause("PROJECTILE", event::entity::DamageCause::PROJECTILE);
    lua_setfield(L, -2, "CAUSE");

    lua_setglobal(L, "events");
}

} // namespace cursestone::plugin
