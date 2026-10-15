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
// BindingsWorld — API of worlds expuesta to the plugins Lua.
//
//   server.worlds()                    list with the names of the
//                                      worlds loaded
//   server.getWorld(name)            object world (nil if not exists)
//   server.loadWorld(name, gen)      load or creates the world; gen is
//                                      "normal" | "flat" | "Nether" |
//                                      "void" (by default "normal")
//   server.defaultWorld()              the world by default (spawn)
//   world.name()                       name of the world
//   world.seed()                       semilla
//   world.dimension()                  0 surface, 1 Nether
//   world.getBlock(x,and,z)              id of the block (0 if not loaded)
//   world.setBlock(x,and,z,id,data)      writes the block (true/false)
//   world.getTime() / setTime(t)       time of the world (0..24000)
//   world.getSpawn()                   x, and, z of the spawn
//   world.getPlayers()                 names of the players inside
//   world.unload()                     unloads the world (false if is
//                                      the by default or is cancela)
//
// just like the rest of the API, the handles not are stored pointers of
// C++: the userdata only carries the name of the world and each call it
// resolves contra server::getWorldByName(). if the world is unloaded,
// the methods devuelven nil/false instead of break.
#include "LuaBindings.h"
#include "Server.h"
#include "Player.h"
#include "world/World.h"
#include <string>
#include <cstring>

namespace cursestone::plugin {

namespace {

// key of the metatable of the handles of world in the registry of Lua.
static const char* kWorldMeta = "CS.World";

/// @brief resolves the World* of the handle in the position `idx`.
///        returns nullptr if the handle is invalid or the world already not
///        is loaded; in that case the methods devuelven nil/false.
World* resolveWorld(lua_State* L, int idx) {
    if (!lua_isuserdata(L, idx))
        return nullptr;
    void* p = lua_touserdata(L, idx);
    if (!p)
        return nullptr;
    luaL_getmetatable(L, kWorldMeta);
    if (lua_getmetatable(L, idx) == 0) {
        lua_pop(L, 1);
        return nullptr;
    }
    bool same = lua_rawequal(L, -1, -2) == 1;
    lua_pop(L, 2);
    if (!same)
        return nullptr;
    const std::string* name = static_cast<const std::string*>(p);
    return Server::getInstance().getWorldByName(*name);
}

/// @brief pushes a handle of world (userdata with the name) or nil.
int pushWorld(lua_State* L, World* w) {
    if (!w) {
        lua_pushnil(L);
        return 1;
    }
    auto* name = static_cast<std::string*>(lua_newuserdata(L, sizeof(std::string)));
    new (name) std::string(w->name());
    luaL_getmetatable(L, kWorldMeta);
    lua_setmetatable(L, -2);
    return 1;
}

// ---------- methods of the object world ----------

int l_world_gc(lua_State* L) {
    if (auto* name = static_cast<std::string*>(lua_touserdata(L, 1))) {
        name->~basic_string();
    }
    return 0;
}

int l_world_tostring(lua_State* L) {
    auto* name = static_cast<std::string*>(lua_touserdata(L, 1));
    lua_pushfstring(L, "world(%s)", name ? name->c_str() : "?");
    return 1;
}

#define WORLD_OR_NIL()                                                                             \
    World* w = resolveWorld(L, 1);                                                                 \
    if (!w) {                                                                                      \
        lua_pushnil(L);                                                                            \
        return 1;                                                                                  \
    }

int l_world_name(lua_State* L) {
    WORLD_OR_NIL();
    lua_pushstring(L, w->name().c_str());
    return 1;
}
int l_world_seed(lua_State* L) {
    WORLD_OR_NIL();
    lua_pushinteger(L, (lua_Integer)w->seed());
    return 1;
}
int l_world_dimension(lua_State* L) {
    WORLD_OR_NIL();
    lua_pushinteger(L, w->dimension());
    return 1;
}
int l_world_gettime(lua_State* L) {
    WORLD_OR_NIL();
    lua_pushinteger(L, w->time());
    return 1;
}

int l_world_settime(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer t = luaL_checkinteger(L, 2);
    t = ((t % 24000) + 24000) % 24000;
    w->setTime((int32_t)t);
    lua_pushboolean(L, 1);
    return 1;
}

int l_world_getblock(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer x = luaL_checkinteger(L, 2);
    lua_Integer y = luaL_checkinteger(L, 3);
    lua_Integer z = luaL_checkinteger(L, 4);
    if (y < 0 || y > 255) {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L, w->getBlockId((int32_t)x, (int32_t)y, (int32_t)z));
    return 1;
}

int l_world_setblock(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer x = luaL_checkinteger(L, 2);
    lua_Integer y = luaL_checkinteger(L, 3);
    lua_Integer z = luaL_checkinteger(L, 4);
    lua_Integer id = luaL_checkinteger(L, 5);
    lua_Integer data = luaL_optinteger(L, 6, 0);
    if (y < 0 || y > 255 || id < 0 || id > 255 || data < 0 || data > 15) {
        lua_pushboolean(L, 0);
        return 1;
    }
    // World::setBlock only writes in chunks resident: is forces the
    // load/generation of the chunk target so that the change never is
    // a not-op silencioso in terrain not yet generado.
    w->chunks().getChunk((int32_t)x >> 4, (int32_t)z >> 4);
    w->setBlock((int32_t)x, (int32_t)y, (int32_t)z, (uint8_t)id, (uint8_t)data);
    lua_pushboolean(L, 1);
    return 1;
}

int l_world_getspawn(lua_State* L) {
    WORLD_OR_NIL();
    auto sp = w->spawn();
    lua_pushnumber(L, (lua_Number)sp.x);
    lua_pushnumber(L, (lua_Number)sp.y);
    lua_pushnumber(L, (lua_Number)sp.z);
    return 3;
}

int l_world_getplayers(lua_State* L) {
    WORLD_OR_NIL();
    auto list = Server::getInstance().playersSnapshot();
    lua_createtable(L, (int)list.size(), 0);
    int i = 1;
    for (auto& p : list) {
        if (p && p->getWorld() == w) {
            lua_pushstring(L, p->getName().c_str());
            lua_rawseti(L, -2, i++);
        }
    }
    return 1;
}

int l_world_unload(lua_State* L) {
    WORLD_OR_NIL();
    lua_pushboolean(L, Server::getInstance().unloadWorld(w->name()) ? 1 : 0);
    return 1;
}

#undef WORLD_OR_NIL

const luaL_Reg kWorldMethods[] = {
    {"name", l_world_name},
    {"seed", l_world_seed},
    {"dimension", l_world_dimension},
    {"getTime", l_world_gettime},
    {"setTime", l_world_settime},
    {"getBlock", l_world_getblock},
    {"setBlock", l_world_setblock},
    {"getSpawn", l_world_getspawn},
    {"getPlayers", l_world_getplayers},
    {"unload", l_world_unload},
    {nullptr, nullptr},
};

// ---------- functions of server.* ----------

int l_server_worlds(lua_State* L) {
    auto worlds = Server::getInstance().getWorlds();
    lua_createtable(L, (int)worlds.size(), 0);
    int i = 1;
    for (World* w : worlds) {
        lua_pushstring(L, w->name().c_str());
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

int l_server_getworld(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    return pushWorld(L, Server::getInstance().getWorldByName(name));
}

int l_server_defaultworld(lua_State* L) {
    return pushWorld(L, Server::getInstance().getDefaultWorld());
}

int l_server_loadworld(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const char* gen = luaL_optstring(L, 2, "normal");
    World* w = Server::getInstance().loadWorld(name, gen);
    return pushWorld(L, w);
}

} // namespace

// Expuesto in LuaBindings.h so that bindCore use the same handle.
void pushWorldHandle(lua_State* L, World* w) {
    pushWorld(L, w);
}

void bindWorld(lua_State* L) {
    // metatable of the handle of world (methods + __gc + __tostring).
    luaL_newmetatable(L, kWorldMeta);
    lua_newtable(L);
    luaL_setfuncs(L, kWorldMethods, 0);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, l_world_gc);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, l_world_tostring);
    lua_setfield(L, -2, "__tostring");
    lua_pop(L, 1);

    // Extiende the tabla global `server` with the functions of worlds.
    lua_getglobal(L, "server");
    if (lua_istable(L, -1)) {
        static const luaL_Reg srvWorldF[] = {
            {"worlds", l_server_worlds},
            {"getWorld", l_server_getworld},
            {"loadWorld", l_server_loadworld},
            {"defaultWorld", l_server_defaultworld},
            {nullptr, nullptr},
        };
        luaL_setfuncs(L, srvWorldF, 0);
    }
    lua_pop(L, 1);
}

} // namespace cursestone::plugin
