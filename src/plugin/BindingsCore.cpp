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
//   server.getPlayer(name)            player handle (nil if not online);
//                                     the handle carries the NAME and each
//                                     method re-resolves the live player:
//                                     p:getName() p:isOnline() p:kill()
//                                     and the player.* actions as methods
//                                     (p:message, p:teleport, p:getHealth, ...)
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
//   player.getPosition(name)          x, y, z (pies) | nil si no existe
//   player.kill(name)                 mata por el pipeline normal de muerte
//                                     (pantalla de muerte, drops, evento);
//                                     false si no existe o ya está muerto
//   player.getRotation(name)          yaw, pitch | nil si no existe
//   player.setRotation(name, yaw, pitch) gira la CÁMARA del jugador: fija
//                                     la rotación en el server y re-sincroniza
//                                     al cliente con un MovePlayerPacket RESET
//                                     (mismo patrón que el revert anti-cheat y
//                                     el teleport; el cliente 0.15 gira la vista
//                                     con ese paquete). false si no existe.
//   player.setMotion(name, x, y, z)   impulso al jugador (empujón/lanza):
//                                     SetEntityMotionPacket con eid=0 al
//                                     propio cliente (convención del protocolo
//                                     84 para el self-motion). false si no existe.
//   scheduler.later(ticks, fn)        runs fn a time; returns id
//   scheduler.every(ticks, fn)        runs fn each ticks; returns id
//   scheduler.cancel(id)              cancela a tarea
//   plugin.api                        version of the API of the server
//   plugin.name / .author / .version  metadata of this plugin
//   plugin.getResource(name)          lee <plugin>/resources/<name>; UNICA
//                                     carpeta de recursos valida (nil+err si no)
//   plugin.saveResource(name, replace) copia resources/<name> a la carpeta de
//                                     datos del plugin (instala el config por
//                                     defecto); true | false, motivo
//   plugin.listResources()            rutas relativas de resources/ (recursivo)
//   import("core/Example")            function global: load a module Lua
//                                     of the own plugin (looks for in src/,
//                                     extension optional, with cache)
//   plugin.include("src/x.lua")       variante explicit of import (path
//                                     exact relativa to its folder, with cache)
//
// REGLAS:
//   - everything by name of player (string). never is saves a player*:
//     is looks for with getPlayerExact in each call. The handles of
//     server.getPlayer() follow the same rule: the userdata only carries
//     the name (same shape as the world handles).
//   - the scheduler runs in the thread main; the functions Lua is
//     are stored as refs in the registry of the state of the plugin.
#include "LuaBindings.h"
#include "LuaPlugin.h"
#include "PathSafe.h"
#include "Server.h"
#include "Player.h"
#include "inventory/PlayerInventory.h"
#include "item/ItemNbt.h"
#include "network/protocol/MovePlayerPacket.h"
#include "network/protocol/SetEntityMotionPacket.h"
#include "utils/MainLogger.hpp"
#include "scheduler/ServerScheduler.h"
#include "scheduler/CallbackTask.h"
#include <filesystem>
#include <fstream>
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
    if (level == 0)
        lg.info(msg);
    else if (level == 1)
        lg.warn(msg);
    else
        lg.error(msg);
    return 0;
}
int l_log_info(lua_State* L) {
    return logWith(L, 0);
}
int l_log_warn(lua_State* L) {
    return logWith(L, 1);
}
int l_log_error(lua_State* L) {
    return logWith(L, 2);
}

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
    if (!p) {
        lua_pushboolean(L, 0);
        return 1;
    }
    p->sendMessage(msg);
    lua_pushboolean(L, 1);
    return 1;
}
int l_player_tip(lua_State* L) {
    Player* p = findPlayer(L, 1);
    const char* msg = luaL_checkstring(L, 2);
    if (!p) {
        lua_pushboolean(L, 0);
        return 1;
    }
    p->sendTip(msg);
    lua_pushboolean(L, 1);
    return 1;
}
int l_player_popup(lua_State* L) {
    Player* p = findPlayer(L, 1);
    const char* msg = luaL_checkstring(L, 2);
    if (!p) {
        lua_pushboolean(L, 0);
        return 1;
    }
    p->sendPopup(msg);
    lua_pushboolean(L, 1);
    return 1;
}
int l_player_kick(lua_State* L) {
    Player* p = findPlayer(L, 1);
    const char* reason = luaL_optstring(L, 2, "Expulsado");
    if (!p) {
        lua_pushboolean(L, 0);
        return 1;
    }
    p->kick(reason);
    lua_pushboolean(L, 1);
    return 1;
}
int l_player_teleport(lua_State* L) {
    Player* p = findPlayer(L, 1);
    lua_Number x = luaL_checknumber(L, 2);
    lua_Number y = luaL_checknumber(L, 3);
    lua_Number z = luaL_checknumber(L, 4);
    if (!p) {
        lua_pushboolean(L, 0);
        return 1;
    }
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
    if (!p) {
        lua_pushboolean(L, 0);
        return 1;
    }
    World* target = Server::getInstance().getWorldByName(worldName);
    if (!target) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, p->switchWorld(target, (double)x, (double)y, (double)z, false) ? 1 : 0);
    return 1;
}
// getWorld(name): handle `world` of the world in that which is the player.
int l_player_getWorld(lua_State* L) {
    Player* p = findPlayer(L, 1);
    if (!p) {
        lua_pushnil(L);
        return 1;
    }
    pushWorldHandle(L, p->getWorld());
    return 1;
}
int l_player_isOp(lua_State* L) {
    Player* p = findPlayer(L, 1);
    lua_pushboolean(L, p ? p->isOp() : 0);
    return 1;
}

// player.getHealth(name) -> hearts (0..20). Dead players report 0.
int l_player_getHealth(lua_State* L) {
    Player* p = findPlayer(L, 1);
    lua_pushnumber(L, p ? p->health() : 0);
    return 1;
}

// player.setHealth(name, hp): PMMP Player::setHealth semantics — clamped
// to [0, max], the hearts bar syncs to the client in the same tick and
// setHealth(0) kills through the regular damage pipeline (death screen,
// drops, PlayerDeathEvent) exactly like /kill. On a dead player it is a
// no-op (only respawn can bring someone back).
int l_player_setHealth(lua_State* L) {
    Player* p = findPlayer(L, 1);
    if (p) {
        lua_Number hp = luaL_checknumber(L, 2);
        p->setHealth((int)hp);
    }
    return 0;
}
int l_player_setPermission(lua_State* L) {
    Player* p = findPlayer(L, 1);
    const char* node = luaL_checkstring(L, 2);
    bool value = lua_toboolean(L, 3) != 0;
    if (!p) {
        lua_pushboolean(L, 0);
        return 1;
    }
    p->setPermission(node, value); // PermissibleBase (in memory)
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
    lua_Integer id = luaL_checkinteger(L, 2);
    lua_Integer count = luaL_optinteger(L, 3, 1);
    lua_Integer meta = luaL_optinteger(L, 4, 0);
    const char* customName = luaL_optstring(L, 5, nullptr);
    if (!p || !p->getInventory()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (id <= 0 || id > 65535 || count < 1) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (count > 2304)
        count = 2304; // 36 slots * 64 — keep it sane

    item::ItemStack st;
    st.id = (uint16_t)id;
    st.count = (int)count;
    st.meta = (uint8_t)(meta & 0xFF);
    if (customName && *customName)
        st.nbt = item::buildDisplayNbt(customName);

    auto leftovers = p->getInventory()->addItem({st});
    bool fit = leftovers.empty() || leftovers[0].isEmpty() || leftovers[0].count < count;
    p->sendInventory(); // full resync — the client renders the new stack
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
    if (!p) {
        lua_pushboolean(L, 0);
        return 1;
    }
    p->setNameTag(tag);
    lua_pushboolean(L, 1);
    return 1;
}
int l_player_getNameTag(lua_State* L) {
    Player* p = findPlayer(L, 1);
    if (!p) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, p->getNameTag().c_str());
    return 1;
}
// setDisplayName(name, dn): chat-only alias (PMMP Player::setDisplayName);
// the chat line uses it, the tag above the head does not.
int l_player_setDisplayName(lua_State* L) {
    Player* p = findPlayer(L, 1);
    const char* dn = luaL_checkstring(L, 2);
    if (!p) {
        lua_pushboolean(L, 0);
        return 1;
    }
    p->setDisplayName(dn);
    lua_pushboolean(L, 1);
    return 1;
}
int l_player_getDisplayName(lua_State* L) {
    Player* p = findPlayer(L, 1);
    if (!p) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, p->getDisplayName().c_str());
    return 1;
}

// player.kill(name) -> boolean. Kills through the regular damage
// pipeline (Player::setHealth(0) -> applyDamage -> onDeath: death screen
// for the client, drops, PlayerDeathEvent + the "<name> died" broadcast).
// false when the player is not online or is already dead (respawn is the
// only way back).
int l_player_kill(lua_State* L) {
    Player* p = findPlayer(L, 1);
    if (!p || p->isDead()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    p->setHealth(0);
    lua_pushboolean(L, 1);
    return 1;
}

// getPosition(name) -> x, y, z (feet) | nil. Values are doubles so plugins
// get the exact server-side position, not a truncated copy.
int l_player_getPosition(lua_State* L) {
    Player* p = findPlayer(L, 1);
    if (!p) {
        lua_pushnil(L);
        return 1;
    }
    const math::Vector3& pos = p->getNextPosition();
    lua_pushnumber(L, (lua_Number)pos.x);
    lua_pushnumber(L, (lua_Number)pos.y);
    lua_pushnumber(L, (lua_Number)pos.z);
    return 3;
}
// getRotation(name) -> yaw, pitch | nil.
int l_player_getRotation(lua_State* L) {
    Player* p = findPlayer(L, 1);
    if (!p) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, (lua_Number)p->yaw());
    lua_pushnumber(L, (lua_Number)p->pitch());
    return 2;
}
// setRotation(name, yaw, pitch) -> bool. Camera control: store the new
// angles server-side, then push a MODE_RESET MovePlayerPacket carrying the
// CURRENT position + the new yaw/pitch — the same packet shape the
// anti-cheat revert and teleport already use, so the 0.15.10 client turns
// the view without moving the player.
int l_player_setRotation(lua_State* L) {
    Player* p = findPlayer(L, 1);
    lua_Number yaw = luaL_checknumber(L, 2);
    lua_Number pitch = luaL_checknumber(L, 3);
    if (!p) {
        lua_pushboolean(L, 0);
        return 1;
    }
    p->setRotation((float)yaw, (float)pitch);
    const math::Vector3& pos = p->getNextPosition();
    p->sendPosition(pos.x, pos.y, pos.z, (double)yaw, (double)pitch,
                    network::protocol::MovePlayerPacket::MODE_RESET);
    lua_pushboolean(L, 1);
    return 1;
}
// setMotion(name, x, y, z) -> bool. Pushes the player. Server state first,
// then SetEntityMotionPacket with eid=0 addressed to the player itself —
// the protocol-84 self-motion form (eid 0 means "this client's player").
int l_player_setMotion(lua_State* L) {
    Player* p = findPlayer(L, 1);
    lua_Number mx = luaL_checknumber(L, 2);
    lua_Number my = luaL_checknumber(L, 3);
    lua_Number mz = luaL_checknumber(L, 4);
    if (!p) {
        lua_pushboolean(L, 0);
        return 1;
    }
    p->setMotion(math::Vector3{(double)mx, (double)my, (double)mz});
    auto pk = std::make_shared<network::protocol::SetEntityMotionPacket>();
    pk->entities.push_back({0, (float)mx, (float)my, (float)mz});
    p->sendPacket(pk);
    lua_pushboolean(L, 1);
    return 1;
}

// ---------- player handle (server.getPlayer) ----------
// server.getPlayer(name) returns the OBJECT counterpart of the name-based
// player.* API: a handle whose userdata carries just the login NAME (same
// shape as the world handles — never a Player*). Every method call
// re-resolves the live player with getPlayerExact, so a handle of a
// player who leaves starts returning nil/false instead of dangling.
// The methods are the name-based functions themselves: the dispatcher
// swaps the handle for its name and jumps into the very same C function,
// so both surfaces can never drift apart.
static const char* kPlayerMeta = "CS.Player";

// resolves the live Player* behind the handle at idx (nullptr when the
// value is not a handle or the player is gone).
Player* resolvePlayerHandle(lua_State* L, int idx) {
    auto* name = static_cast<std::string*>(luaL_testudata(L, idx, kPlayerMeta));
    if (!name)
        return nullptr;
    return Server::getInstance().getPlayerExact(*name);
}

int l_player_handle_gc(lua_State* L) {
    if (auto* name = static_cast<std::string*>(lua_touserdata(L, 1))) {
        name->~basic_string();
    }
    return 0;
}

int l_player_handle_tostring(lua_State* L) {
    auto* name = static_cast<std::string*>(lua_touserdata(L, 1));
    lua_pushfstring(L, "player(%s)", name ? name->c_str() : "?");
    return 1;
}

// server.getPlayer(name) -> handle | nil. nil when no online player
// matches (the lookup is the same case-insensitive one the name-based
// API uses).
int l_server_getplayer(lua_State* L) {
    const char* req = luaL_checkstring(L, 1);
    Player* p = Server::getInstance().getPlayerExact(req);
    if (!p) {
        lua_pushnil(L);
        return 1;
    }
    auto* stored = static_cast<std::string*>(lua_newuserdata(L, sizeof(std::string)));
    new (stored) std::string(p->getName());
    luaL_getmetatable(L, kPlayerMeta);
    lua_setmetatable(L, -2);
    return 1;
}

// generic method dispatch: (self, ...) -> impl(name, ...). Upvalue 1 is
// the C function of the name-based API; the handle becomes its first
// string argument and the rest of the stack stays untouched.
int l_player_method(lua_State* L) {
    auto* name = static_cast<std::string*>(luaL_checkudata(L, 1, kPlayerMeta));
    std::string held = *name; // copy: the stack mutates
    lua_pop(L, 1);            // drop the handle
    lua_pushstring(L, held.c_str());
    lua_insert(L, 1); // name first, args follow
    auto impl = (lua_CFunction)lua_tocfunction(L, lua_upvalueindex(1));
    return impl(L);
}

// p:getName() — the login name the handle carries; works even after the
// player left (pair it with isOnline()).
int l_player_handle_getname(lua_State* L) {
    auto* name = static_cast<std::string*>(luaL_checkudata(L, 1, kPlayerMeta));
    lua_pushlstring(L, name->data(), name->size());
    return 1;
}

// p:isOnline() — true while the player is still connected.
int l_player_handle_isonline(lua_State* L) {
    lua_pushboolean(L, resolvePlayerHandle(L, 1) != nullptr);
    return 1;
}

// metatable CS.Player: mirrored methods (dispatchers around the
// name-based functions) + getName/isOnline + __gc/__tostring.
void bindPlayerHandle(lua_State* L) {
    luaL_newmetatable(L, kPlayerMeta);
    lua_newtable(L); // __index
    static const struct {
        const char* name;
        lua_CFunction impl;
    } mirror[] = {
        {"message", l_player_message},
        {"tip", l_player_tip},
        {"popup", l_player_popup},
        {"kick", l_player_kick},
        {"teleport", l_player_teleport},
        {"teleportWorld", l_player_teleportWorld},
        {"getWorld", l_player_getWorld},
        {"isOp", l_player_isOp},
        {"getHealth", l_player_getHealth},
        {"setHealth", l_player_setHealth},
        {"setPermission", l_player_setPermission},
        {"hasPermission", l_player_hasPermission},
        {"giveItem", l_player_giveItem},
        {"setNameTag", l_player_setNameTag},
        {"getNameTag", l_player_getNameTag},
        {"setDisplayName", l_player_setDisplayName},
        {"getDisplayName", l_player_getDisplayName},
        {"getPosition", l_player_getPosition},
        {"getRotation", l_player_getRotation},
        {"setRotation", l_player_setRotation},
        {"setMotion", l_player_setMotion},
        {"kill", l_player_kill},
    };
    for (const auto& m : mirror) {
        lua_pushcfunction(L, m.impl); // upvalue: the name-based impl
        lua_pushcclosure(L, l_player_method, 1);
        lua_setfield(L, -2, m.name);
    }
    static const luaL_Reg special[] = {{"getName", l_player_handle_getname},
                                       {"isOnline", l_player_handle_isonline},
                                       {nullptr, nullptr}};
    luaL_setfuncs(L, special, 0);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, l_player_handle_gc);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, l_player_handle_tostring);
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);
}

int l_plugin_getResource(lua_State* L) {
    LuaPlugin* pl = LuaPlugin::from(L);
    const char* req = luaL_checkstring(L, 1);
    std::string rel;
    if (!sanitizeRelPath(req, rel)) {
        return luaL_error(L,
                          "plugin.getResource: invalid path '%s' (only relative paths "
                          "inside resources/, no '..')",
                          req);
    }
    std::string path = pl->resourcesDir() + "/" + rel;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        lua_pushnil(L);
        lua_pushfstring(L, "resource '%s' does not exist", rel.c_str());
        return 2;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        lua_pushnil(L);
        lua_pushfstring(L, "could not open resource '%s'", rel.c_str());
        return 2;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    lua_pushlstring(L, content.data(), content.size());
    return 1;
}
// saveResource(name, replace) -> true | false, reason. Installs a bundled
// resource into the plugin's data folder (the default-config flow: ship
// resources/config.yml, call saveResource("config.yml") in onEnable, then
// data.loadYml("config.yml")). Parent folders are created; an existing
// file is only overwritten when replace is truthy.
int l_plugin_saveResource(lua_State* L) {
    namespace fs = std::filesystem;
    LuaPlugin* pl = LuaPlugin::from(L);
    const char* req = luaL_checkstring(L, 1);
    bool replace = lua_toboolean(L, 2) != 0;
    std::string rel;
    if (!sanitizeRelPath(req, rel)) {
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "invalid path '%s' (only relative paths inside resources/)", req);
        return 2;
    }
    std::string src = pl->resourcesDir() + "/" + rel;
    std::string dst = pl->dataDir() + "/" + rel;
    std::error_code ec;
    if (!fs::exists(src, ec)) {
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "resource '%s' does not exist", rel.c_str());
        return 2;
    }
    if (fs::exists(dst, ec) && !replace) {
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "'%s' already exists in the data folder (pass true to overwrite)",
                        rel.c_str());
        return 2;
    }
    fs::create_directories(fs::path(dst).parent_path(), ec);
    if (ec) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, ec.message().c_str());
        return 2;
    }
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, ec.message().c_str());
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}
// listResources() -> { "config.yml", "layouts/hud.txt", ... } — relative
// paths, recursive, ready to feed back into getResource()/saveResource().
int l_plugin_listResources(lua_State* L) {
    namespace fs = std::filesystem;
    std::string root = LuaPlugin::from(L)->resourcesDir();
    lua_createtable(L, 0, 0);
    int i = 1;
    std::error_code ec;
    if (fs::exists(root, ec)) {
        for (auto& entry : fs::recursive_directory_iterator(root, ec)) {
            std::error_code e2;
            if (!entry.is_regular_file(e2))
                continue;
            std::string rel = fs::relative(entry.path(), root, e2).string();
            if (e2 || rel.empty())
                continue;
            lua_pushstring(L, rel.c_str());
            lua_rawseti(L, -2, i++);
        }
    }
    return 1;
}

// ---------- scheduler ----------
// common to later() and every(): save the function as ref, schedule a
// CallbackTask in the ServerScheduler and return the id of tarea to Lua.
int scheduleImpl(lua_State* L, bool repeat) {
    LuaPlugin* pl = LuaPlugin::from(L);
    int ticks = (int)luaL_checkinteger(L, 1);
    if (ticks < 1)
        ticks = 1;
    luaL_checktype(L, 2, LUA_TFUNCTION);

    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);      // save the function
    auto idHolder = std::make_shared<uint64_t>(0); // the id is is known after schedule

    auto task = std::make_shared<scheduler::CallbackTask>([pl, ref, repeat, idHolder]() {
        if (!pl->isEnabled())
            return; // plugin already deshabilitado
        lua_rawgeti(pl->L(), LUA_REGISTRYINDEX, ref);
        pl->pcall(0, 0);
        if (!repeat)
            pl->releaseTask(*idHolder); // one-shot: soltar the ref
    });

    uint64_t id = scheduler::ServerScheduler::get().scheduleSync(task, ticks, repeat ? ticks : 0);
    *idHolder = id;
    pl->addTask(id, ref);
    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}
int l_scheduler_later(lua_State* L) {
    return scheduleImpl(L, false);
}
int l_scheduler_every(lua_State* L) {
    return scheduleImpl(L, true);
}
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

    lua_pushstring(L, CURSESTONE_API); // plugin.api
    lua_setfield(L, -2, "api");
    lua_pushstring(L, pl->name().c_str()); // plugin.name
    lua_setfield(L, -2, "name");
    lua_pushstring(L, pl->author().c_str()); // plugin.author
    lua_setfield(L, -2, "author");
    lua_pushstring(L, pl->version().c_str()); // plugin.version
    lua_setfield(L, -2, "version");

    static const luaL_Reg pluginF[] = {{"include", l_plugin_include},
                                       {"getResource", l_plugin_getResource},
                                       {"saveResource", l_plugin_saveResource},
                                       {"listResources", l_plugin_listResources},
                                       {nullptr, nullptr}};
    luaL_setfuncs(L, pluginF, 0);

    lua_setglobal(L, "plugin");
}

} // namespace

void bindCore(lua_State* L) {
    static const luaL_Reg logF[] = {
        {"info", l_log_info}, {"warn", l_log_warn}, {"error", l_log_error}, {nullptr, nullptr}};
    static const luaL_Reg srvF[] = {{"broadcast", l_server_broadcast},
                                    {"players", l_server_players},
                                    {"getPlayer", l_server_getplayer},
                                    {nullptr, nullptr}};
    static const luaL_Reg plrF[] = {{"message", l_player_message},
                                    {"tip", l_player_tip},
                                    {"popup", l_player_popup},
                                    {"kick", l_player_kick},
                                    {"teleport", l_player_teleport},
                                    {"teleportWorld", l_player_teleportWorld},
                                    {"getWorld", l_player_getWorld},
                                    {"isOp", l_player_isOp},
                                    {"getHealth", l_player_getHealth},
                                    {"setHealth", l_player_setHealth},
                                    {"setPermission", l_player_setPermission},
                                    {"hasPermission", l_player_hasPermission},
                                    {"giveItem", l_player_giveItem},
                                    {"setNameTag", l_player_setNameTag},
                                    {"getNameTag", l_player_getNameTag},
                                    {"setDisplayName", l_player_setDisplayName},
                                    {"getDisplayName", l_player_getDisplayName},
                                    {"getPosition", l_player_getPosition},
                                    {"getRotation", l_player_getRotation},
                                    {"setRotation", l_player_setRotation},
                                    {"setMotion", l_player_setMotion},
                                    {"kill", l_player_kill},
                                    {nullptr, nullptr}};
    static const luaL_Reg schF[] = {{"later", l_scheduler_later},
                                    {"every", l_scheduler_every},
                                    {"cancel", l_scheduler_cancel},
                                    {nullptr, nullptr}};
    setTable(L, "log", logF);
    setTable(L, "server", srvF);
    setTable(L, "player", plrF);
    setTable(L, "scheduler", schF);
    bindPlayerHandle(L); // metatable CS.Player for server.getPlayer
    bindPluginTable(L);  // plugin.api / .name / .author / .version / .include

    // import("module"): load of módulos Lua of the own plugin (the way
    // recommended; looks for in src/ and not requires extension). global, as requires
    // the flujo natural: local Example = import("core/Example").
    lua_pushcfunction(L, l_plugin_import);
    lua_setglobal(L, "import");
}

} // namespace cursestone::plugin
