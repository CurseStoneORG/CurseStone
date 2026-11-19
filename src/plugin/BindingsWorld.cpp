/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
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
// BindingsWorld — API of worlds expuesta to the plugins Lua.
//
//   server.worlds()                    list with the names of the
//                                      worlds loaded
//   server.getWorld(name)            object world (nil if not exists)
//   server.loadWorld(name, gen, [seed])  load or creates the world; gen is
//                                      "normal" | "flat" | "nether" |
//                                      "void" | "empty" (by default
//                                      "normal"); seed 0 derives one from
//                                      the clock. New worlds persist in
//                                      worlds/worlds.manifest and reload
//                                      after a restart
//   server.defaultWorld()              the world by default (spawn)
//   world.name()                       name of the world
//   world.seed()                       semilla
//   world.dimension()                  0 surface, 1 Nether
//   world.getBlock(x,and,z)              id of the block (0 if not loaded)
//   world.setBlock(x,and,z,id,data)      writes the block (true/false)
//   world.getTime() / setTime(t)       time of the world (0..24000)
//   world.getSpawn()                   x, and, z of the spawn
//   world.getPlayers()                 names of the players inside
//   world.spawnParticle(x,and,z,type,[data])   spawns one protocol-84
//                                      particle; type is a name ("flame",
//                                      "heart", "terrain", ...) or a raw
//                                      id 1..32; data is the particle
//                                      payload (scale/color/packing, see
//                                      docs/PLUGINS.md). true = sent.
//   world.spawnBlockParticles(x,and,z,id,[meta])  the break burst + dig
//                                      sound at a block (event 2001,
//                                      id low byte, meta in bits 12+).
//   world.spawnSplashParticles(x,and,z,rgb)    the splash-potion shatter
//                                      (event 2002) with an 0xRRGGBB
//                                      color; plays the glass smash.
//   world.spawnFloatingText(x,y,z,text,[ttl])  floating text particle:
//                                      an invisible fake-player carrier
//                                      whose nametag is the text (this
//                                      client era draws nametags only on
//                                      player entities, and protocol 84
//                                      has no text particle). '\n' splits
//                                      the text into the title line and
//                                      the body lines the client stacks
//                                      below it. Returns the entity id, or
//                                      nil at the per-world cap; ttl in
//                                      ticks (0 = until removed). Players
//                                      that enter render range later
//                                      receive it too (one viewer sync per
//                                      second).
//   world.moveFloatingText(id,x,y,z)   repositions a floating text
//                                      (true/false).
//   world.setFloatingText(id,text)     replaces the text (same '\n'
//                                      multi-line model); one
//                                      SetEntityDataPacket resyncs the
//                                      viewers (true/false).
//   world.setFloatingTextTitle(id,t)   replaces only the title line (the
//                                      Genisys setTitle; true/false).
//   world.setFloatingTextBody(id,t)    replaces only the body lines (the
//                                      Genisys setText; true/false).
//   world.setFloatingTextVisible(id,b) hides (b=false: one Remove to the
//                                      viewers) or shows (b=true: re-arm
//                                      now) a text (true/false).
//   world.setFloatingTextFollow(id,eid)  anchors the text over one
//                                      entity or player (0 = fixed spot
//                                      again); the follow pass keeps it
//                                      there (true/false).
//   world.setFloatingTextFollowPlayer(id,name)
//                                      anchors the text over a player
//                                      resolved by name now
//                                      (true/false).
//   world.setFloatingTextViewers(id,t) viewer allow list: t is a table
//                                      of player names (nil/{} = every
//                                      player renders it) (true/false).
//   world.getFloatingTexts()           array of tables
//                                      {id,x,y,z,text,title,body,
//                                      visible,follow} of this world
//                                      (text = title + '\n' + body).
//   world.removeFloatingText(id)       despawns a floating text
//                                      (true/false).
//   world.spawnEntity(type,x,y,z,[yaw],[pitch])
//                                      spawns one standing mob (no AI,
//                                      no physics, zero per-tick cost).
//                                      type is a name ("zombie", "cow",
//                                      ...) or a protocol-84 network id;
//                                      see world.entityTypes(). Returns
//                                      the entity id, or false.
//   world.moveEntity(id,x,y,z,[yaw])   repositions a mob (true/false).
//   world.removeEntity(id)             despawns a mob (true/false).
//   world.getEntity(id)                table {id,type,name,x,y,z,yaw,pitch}
//                                      or nil.
//   world.getEntities()                array of those tables.
//   world.entityTypes()                map name -> protocol-84 id for the
//                                      33 supported mobs.
//   world.unload()                     unloads the world (false if is
//                                      the by default or is cancela)
//   world.save()                       flushes dirty chunks + level.dat to
//                                      disk (true/false)
//   world.playerCount()                how many players stand in the world
//   world.isChunkLoaded(cx,cz)         chunk-COORDINATE residency probe
//                                      (true = the chunk is in the cache)
//   world.chunkCount()                 chunks currently resident (the
//                                      chunk-cache size)
//   world.generator()                  the generator token ("normal",
//                                      "flat", "nether", "void", "empty")
//   world.isDefault()                  true for the default (spawn) world;
//                                      the default can never unload
//   world.setSpawn(x,y,z)              moves the world spawn (compass
//                                      target for players inside)
//
//   server.unloadWorld(name)           unloads the world by name (false
//                                      for the default / cancela)
//   server.isWorldLoaded(name)         true when the world is loaded —
//                                      unambiguous vs getWorld() == nil
//
// just like the rest of the API, the handles not are stored pointers of
// C++: the userdata only carries the name of the world and each call it
// resolves contra server::getWorldByName(). if the world is unloaded,
// the methods devuelven nil/false instead of break.
#include "LuaBindings.h"
#include "Server.h"
#include "Player.h"
#include "world/World.h"
#include "world/particle/ParticleNames.h"
#include "entity/EntityTypes.h"
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
    // Push the new time out RIGHT NOW instead of waiting for the world's
    // next periodic re-anchor (up to TIME_RESYNC_INTERVAL ticks): the
    // client binds its clock to the last SetTime it processed, so a
    // plugin-driven time change that only reached the server left every
    // viewer of this world on the old sky for up to 10 seconds.
    w->broadcastTimeSync();
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

// ---- multiworld extension: cache, spawn, generator, default flag ----

int l_world_save(lua_State* L) {
    WORLD_OR_NIL();
    lua_pushboolean(L, w->save() >= 0 ? 1 : 0);
    return 1;
}

int l_world_playercount(lua_State* L) {
    WORLD_OR_NIL();
    int n = 0;
    for (const auto& p : Server::getInstance().playersSnapshotRef()) {
        if (p && p->getWorld() == w)
            ++n;
    }
    lua_pushinteger(L, n);
    return 1;
}

// Chunk coordinates (block >> 4), matching the native isChunkLoaded.
int l_world_ischunkloaded(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer cx = luaL_checkinteger(L, 2);
    lua_Integer cz = luaL_checkinteger(L, 3);
    lua_pushboolean(L, w->chunks().getChunkIfLoaded((int32_t)cx, (int32_t)cz) != nullptr ? 1 : 0);
    return 1;
}

int l_world_chunkcount(lua_State* L) {
    WORLD_OR_NIL();
    lua_pushinteger(L, (lua_Integer)w->chunkCount());
    return 1;
}

int l_world_generator(lua_State* L) {
    WORLD_OR_NIL();
    lua_pushstring(L, w->config().generator.c_str());
    return 1;
}

int l_world_isdefault(lua_State* L) {
    WORLD_OR_NIL();
    lua_pushboolean(L, Server::getInstance().getDefaultWorld() == w ? 1 : 0);
    return 1;
}

int l_world_setspawn(lua_State* L) {
    WORLD_OR_NIL();
    lua_Number x = luaL_checknumber(L, 2);
    lua_Number y = luaL_checknumber(L, 3);
    lua_Number z = luaL_checknumber(L, 4);
    w->setSpawn(mcp_v3((float)x, (float)y, (float)z));
    lua_pushboolean(L, 1);
    return 1;
}

// Resolves the `type` argument of spawnParticle: a name string goes through
// the shared protocol-84 registry, a number must already be an id 1..32.
// Pushes nothing; returns the resolved id or -1 (Lua's spawnParticle turns
// that into `false`, never an error).
int resolveParticleType(lua_State* L, int idx) {
    if (lua_type(L, idx) == LUA_TSTRING) {
        int32_t id = -1;
        world::particle::particleFromName(lua_tostring(L, idx), id);
        return id;
    }
    if (!lua_isnumber(L, idx))
        return -1;
    lua_Integer id = lua_tointeger(L, idx);
    return world::particle::isValidParticleType((int32_t)id) ? (int32_t)id : -1;
}

int l_world_spawnparticle(lua_State* L) {
    WORLD_OR_NIL();
    lua_Number x = luaL_checknumber(L, 2);
    lua_Number y = luaL_checknumber(L, 3);
    lua_Number z = luaL_checknumber(L, 4);
    int type = resolveParticleType(L, 5);
    lua_Integer data = luaL_optinteger(L, 6, 0);
    lua_pushboolean(L, type >= 0 && w->spawnParticle(type, (float)x, (float)y, (float)z,
                                                     (int32_t)data)
                                     ? 1
                                     : 0);
    return 1;
}

int l_world_spawnblockparticles(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer x = luaL_checkinteger(L, 2);
    lua_Integer y = luaL_checkinteger(L, 3);
    lua_Integer z = luaL_checkinteger(L, 4);
    lua_Integer id = luaL_checkinteger(L, 5);
    lua_Integer meta = luaL_optinteger(L, 6, 0);
    if (id < 0 || id > 255 || meta < 0 || meta > 15) {
        lua_pushboolean(L, 0);
        return 1;
    }
    w->broadcastBlockParticles((int32_t)x, (int32_t)y, (int32_t)z, (uint8_t)id, (uint8_t)meta);
    lua_pushboolean(L, 1);
    return 1;
}

int l_world_splashparticles(lua_State* L) {
    WORLD_OR_NIL();
    lua_Number x = luaL_checknumber(L, 2);
    lua_Number y = luaL_checknumber(L, 3);
    lua_Number z = luaL_checknumber(L, 4);
    lua_Integer rgb = luaL_checkinteger(L, 5);
    lua_pushboolean(L, rgb >= 0 && rgb <= 0xFFFFFF &&
                           w->spawnSplashParticles((float)x, (float)y, (float)z, (uint32_t)rgb)
                       ? 1
                       : 0);
    return 1;
}

int l_world_spawnfloatingtext(lua_State* L) {
    WORLD_OR_NIL();
    lua_Number x = luaL_checknumber(L, 2);
    lua_Number y = luaL_checknumber(L, 3);
    lua_Number z = luaL_checknumber(L, 4);
    size_t len = 0;
    const char* text = luaL_checklstring(L, 5, &len);
    lua_Integer ttl = luaL_optinteger(L, 6, 0);
    int64_t id = w->spawnFloatingText((double)x, (double)y, (double)z,
                                      std::string(text, len), (int32_t)ttl);
    if (id < 0) {
        lua_pushnil(L); // per-world cap reached
    } else {
        lua_pushinteger(L, (lua_Integer)id);
    }
    return 1;
}

int l_world_movefloatingtext(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer id = luaL_checkinteger(L, 2);
    lua_Number x = luaL_checknumber(L, 3);
    lua_Number y = luaL_checknumber(L, 4);
    lua_Number z = luaL_checknumber(L, 5);
    lua_pushboolean(
        L, w->moveFloatingText((int64_t)id, (double)x, (double)y, (double)z) ? 1 : 0);
    return 1;
}

int l_world_setfloatingtext(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer id = luaL_checkinteger(L, 2);
    size_t len = 0;
    const char* text = luaL_checklstring(L, 3, &len);
    lua_pushboolean(
        L, w->setFloatingText((int64_t)id, std::string(text, len)) ? 1 : 0);
    return 1;
}

// Genisys-style single-field re-text helpers share the same lookup +
// boolean reply shape; the class-level clamps (title 128, pair 512 chars)
// apply inside.
int l_world_setfloatingtexttitle(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer id = luaL_checkinteger(L, 2);
    size_t len = 0;
    const char* title = luaL_checklstring(L, 3, &len);
    lua_pushboolean(
        L, w->setFloatingTextTitle((int64_t)id, std::string(title, len)) ? 1 : 0);
    return 1;
}

int l_world_setfloatingtextbody(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer id = luaL_checkinteger(L, 2);
    size_t len = 0;
    const char* body = luaL_checklstring(L, 3, &len);
    lua_pushboolean(
        L, w->setFloatingTextBody((int64_t)id, std::string(body, len)) ? 1 : 0);
    return 1;
}

int l_world_setfloatingtextvisible(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer id = luaL_checkinteger(L, 2);
    lua_pushboolean(L, w->setFloatingTextVisible((int64_t)id, lua_toboolean(L, 3) != 0)
                           ? 1
                           : 0);
    return 1;
}

int l_world_setfloatingtextfollow(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer id = luaL_checkinteger(L, 2);
    lua_Integer target = luaL_optinteger(L, 3, 0);
    lua_pushboolean(
        L, w->setFloatingTextFollow((int64_t)id, (int64_t)target) ? 1 : 0);
    return 1;
}

int l_world_setfloatingtextfollowplayer(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer id = luaL_checkinteger(L, 2);
    const char* name = luaL_checkstring(L, 3);
    lua_pushboolean(
        L, w->setFloatingTextFollowPlayer((int64_t)id, name) ? 1 : 0);
    return 1;
}

// setFloatingTextViewers(id, {"A", "B"}) arms the allow list; nil, an
// empty table or a table with no string entries restores "everyone".
int l_world_setfloatingtextviewers(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer id = luaL_checkinteger(L, 2);
    std::vector<std::string> names;
    if (!lua_isnoneornil(L, 3)) {
        luaL_checktype(L, 3, LUA_TTABLE);
        lua_pushnil(L);
        while (lua_next(L, 3) != 0) {
            if (lua_isstring(L, -1)) {
                size_t len = 0;
                const char* s = lua_tolstring(L, -1, &len);
                names.emplace_back(s, len);
            }
            lua_pop(L, 1);
        }
    }
    lua_pushboolean(
        L, w->setFloatingTextViewers((int64_t)id, std::move(names)) ? 1 : 0);
    return 1;
}

int l_world_getfloatingtexts(lua_State* L) {
    WORLD_OR_NIL();
    auto list = w->floatingTexts();
    lua_createtable(L, (int)list.size(), 0);
    int i = 1;
    for (const auto& t : list) {
        lua_createtable(L, 0, 9);
        lua_pushinteger(L, (lua_Integer)t.id);
        lua_setfield(L, -2, "id");
        lua_pushnumber(L, (lua_Number)t.x);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, (lua_Number)t.y);
        lua_setfield(L, -2, "y");
        lua_pushnumber(L, (lua_Number)t.z);
        lua_setfield(L, -2, "z");
        lua_pushstring(L, t.text.c_str());
        lua_setfield(L, -2, "text");
        lua_pushstring(L, t.title.c_str());
        lua_setfield(L, -2, "title");
        lua_pushstring(L, t.body.c_str());
        lua_setfield(L, -2, "body");
        lua_pushboolean(L, t.visible ? 1 : 0);
        lua_setfield(L, -2, "visible");
        lua_pushinteger(L, (lua_Integer)t.followTarget);
        lua_setfield(L, -2, "follow");
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

int l_world_removefloatingtext(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer id = luaL_checkinteger(L, 2);
    lua_pushboolean(L, w->removeFloatingText((int64_t)id) ? 1 : 0);
    return 1;
}

// ---------- standing mobs ----------

// Resolves the `type` argument of spawnEntity: a name string goes through
// the shared mob registry (case-insensitive, aliases included), a number
// must already be a protocol-84 network id. Returns the id or -1.
int resolveEntityType(lua_State* L, int idx) {
    if (lua_type(L, idx) == LUA_TSTRING) {
        int32_t id = -1;
        entity::mobTypeFromName(lua_tostring(L, idx), &id);
        return id;
    }
    if (!lua_isnumber(L, idx))
        return -1;
    lua_Integer id = lua_tointeger(L, idx);
    return entity::isValidMobType((int32_t)id) ? (int32_t)id : -1;
}

// Pushes the table shape shared by getEntity and getEntities, or nil for
// an unknown record. The record pointer is stable while the mob stays
// registered; Lua runs on the main thread like every world mutation, so
// the field copies below cannot race a removal.
int pushMobTable(lua_State* L, const world::MobEntity* m) {
    if (!m) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 8);
    lua_pushinteger(L, (lua_Integer)m->eid);
    lua_setfield(L, -2, "id");
    lua_pushinteger(L, m->type);
    lua_setfield(L, -2, "type");
    const char* name = entity::mobTypeName(m->type);
    lua_pushstring(L, name ? name : "");
    lua_setfield(L, -2, "name");
    lua_pushnumber(L, (lua_Number)m->x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, (lua_Number)m->y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, (lua_Number)m->z);
    lua_setfield(L, -2, "z");
    lua_pushnumber(L, (lua_Number)m->yaw);
    lua_setfield(L, -2, "yaw");
    lua_pushnumber(L, (lua_Number)m->pitch);
    lua_setfield(L, -2, "pitch");
    return 1;
}

int l_world_spawnentity(lua_State* L) {
    WORLD_OR_NIL();
    int type = resolveEntityType(L, 2);
    lua_Number x = luaL_checknumber(L, 3);
    lua_Number y = luaL_checknumber(L, 4);
    lua_Number z = luaL_checknumber(L, 5);
    lua_Number yaw = luaL_optnumber(L, 6, 0.0);
    lua_Number pitch = luaL_optnumber(L, 7, 0.0);
    if (type < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    int64_t eid = w->spawnMob(type, (double)x, (double)y, (double)z, (float)yaw,
                              (float)pitch);
    if (eid < 0) {
        lua_pushboolean(L, 0); // cap reached / invalid position
    } else {
        lua_pushinteger(L, (lua_Integer)eid);
    }
    return 1;
}

int l_world_moveentity(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer id = luaL_checkinteger(L, 2);
    lua_Number x = luaL_checknumber(L, 3);
    lua_Number y = luaL_checknumber(L, 4);
    lua_Number z = luaL_checknumber(L, 5);
    lua_Number yaw = luaL_optnumber(L, 6, -9999.0);
    lua_Number pitch = luaL_optnumber(L, 7, -9999.0);
    // Optional yaw/pitch: the sentinel keeps the recorded rotation when
    // the caller only wants to reposition.
    float useYaw, usePitch;
    {
        world::MobEntity* m = w->mob((int64_t)id);
        if (!m) {
            lua_pushboolean(L, 0);
            return 1;
        }
        useYaw = (yaw <= -9998.0) ? m->yaw : (float)yaw;
        usePitch = (pitch <= -9998.0) ? m->pitch : (float)pitch;
    }
    lua_pushboolean(L, w->moveMob((int64_t)id, (double)x, (double)y, (double)z, useYaw,
                                  usePitch)
                        ? 1
                        : 0);
    return 1;
}

int l_world_removeentity(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer id = luaL_checkinteger(L, 2);
    lua_pushboolean(L, w->removeMob((int64_t)id) ? 1 : 0);
    return 1;
}

int l_world_getentity(lua_State* L) {
    WORLD_OR_NIL();
    lua_Integer id = luaL_checkinteger(L, 2);
    return pushMobTable(L, w->mob((int64_t)id));
}

int l_world_getentities(lua_State* L) {
    WORLD_OR_NIL();
    auto list = w->mobs();
    lua_createtable(L, (int)list.size(), 0);
    int i = 1;
    for (const world::MobEntity* m : list) {
        pushMobTable(L, m);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

int l_world_entitytypes(lua_State* L) {
    // name -> protocol-84 network id, straight from the shared registry
    // so the Lua surface can never drift from the eggs/native one.
    static const char* kNames[] = {
        "chicken",   "cow",             "pig",             "sheep",
        "wolf",      "villager",        "mooshroom",       "squid",
        "rabbit",    "bat",             "ocelot",          "horse",
        "donkey",    "mule",            "skeleton_horse",  "zombie_horse",
        "zombie",    "creeper",         "skeleton",        "spider",
        "zombie_pigman", "slime",       "enderman",        "silverfish",
        "ghast",     "lava_slime",      "magma_cube",      "blaze",
        "zombie_villager", "witch",     "stray",           "husk",
        "wither_skeleton",
    };
    lua_createtable(L, 0, (int)(sizeof(kNames) / sizeof(kNames[0])));
    for (const char* n : kNames) {
        int32_t id = -1;
        if (entity::mobTypeFromName(n, &id)) {
            lua_pushinteger(L, id);
            lua_setfield(L, -2, n);
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
    {"generator", l_world_generator},
    {"isDefault", l_world_isdefault},
    {"getTime", l_world_gettime},
    {"setTime", l_world_settime},
    {"getBlock", l_world_getblock},
    {"setBlock", l_world_setblock},
    {"getSpawn", l_world_getspawn},
    {"setSpawn", l_world_setspawn},
    {"getPlayers", l_world_getplayers},
    {"playerCount", l_world_playercount},
    {"isChunkLoaded", l_world_ischunkloaded},
    {"chunkCount", l_world_chunkcount},
    {"save", l_world_save},
    {"spawnParticle", l_world_spawnparticle},
    {"spawnBlockParticles", l_world_spawnblockparticles},
    {"spawnSplashParticles", l_world_splashparticles},
    {"spawnFloatingText", l_world_spawnfloatingtext},
    {"moveFloatingText", l_world_movefloatingtext},
    {"setFloatingText", l_world_setfloatingtext},
    {"setFloatingTextTitle", l_world_setfloatingtexttitle},
    {"setFloatingTextBody", l_world_setfloatingtextbody},
    {"setFloatingTextVisible", l_world_setfloatingtextvisible},
    {"setFloatingTextFollow", l_world_setfloatingtextfollow},
    {"setFloatingTextFollowPlayer", l_world_setfloatingtextfollowplayer},
    {"setFloatingTextViewers", l_world_setfloatingtextviewers},
    {"getFloatingTexts", l_world_getfloatingtexts},
    {"removeFloatingText", l_world_removefloatingtext},
    {"spawnEntity", l_world_spawnentity},
    {"moveEntity", l_world_moveentity},
    {"removeEntity", l_world_removeentity},
    {"getEntity", l_world_getentity},
    {"getEntities", l_world_getentities},
    {"entityTypes", l_world_entitytypes},
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
    lua_Integer seed = luaL_optinteger(L, 3, 0);
    World* w = Server::getInstance().loadWorld(name, gen, (int64_t)seed);
    return pushWorld(L, w);
}

// server.unloadWorld(name): unloads a loaded world (players move to the
// default world's spawn). The default world refuses to unload.
int l_server_unloadworld(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    lua_pushboolean(L, Server::getInstance().unloadWorld(name) ? 1 : 0);
    return 1;
}

// server.isWorldLoaded(name): true/false — unlike getWorld() (nil) this is
// unambiguous in a single push and does not allocate a handle.
int l_server_isworldloaded(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    lua_pushboolean(L, Server::getInstance().getWorldByName(name) != nullptr ? 1 : 0);
    return 1;
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
            {"unloadWorld", l_server_unloadworld},
            {"isWorldLoaded", l_server_isworldloaded},
            {"defaultWorld", l_server_defaultworld},
            {nullptr, nullptr},
        };
        luaL_setfuncs(L, srvWorldF, 0);
    }
    lua_pop(L, 1);
}

} // namespace cursestone::plugin
