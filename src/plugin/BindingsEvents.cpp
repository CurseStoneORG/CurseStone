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
// Lua event bindings and supported event names.
//
// events.on("PlayerJoin", function(e) ... end)
// events.on("PlayerQuit", function(e) ... end)
// events.on("PlayerChat", function(e) ... end)
// events.on("PlayerMove", function(e) ... end)
// events.on("PlayerDropItem", function(e) ... end)
// events.on("PlayerItemHeld", function(e) ... end)
// events.on("BlockBreak", function(e) ... end)
// events.on("BlockPlace", function(e) ... end)
// events.on("EntityDamage", function(e) ... end)
// events.on("EntityDamageByEntity", function(e) ... end)
// events.on("PlayerDeath", function(e) ... end)
// events.on("PlayerRespawn", function(e) ... end)
// events.on("PlayerBedEnter", function(e) ... end)
// events.on("PlayerSleepInBed", function(e) ... end) -- Alias for PlayerBedEnter.
// events.on("PlayerBedLeave", function(e) ... end)
// events.on("DoorToggle", function(e) ... end)
// events.on("PlayerInteract", function(e) ... end)
//
// Event fields:
// - Player events: e.player and e.message.
// - PlayerMove: e.player and fromX/fromY/fromZ/toX/toY/toZ.
// - Item events: e.item_id, e.item_meta, and e.item_count.
// - Block events: e.player, e.x, e.y, e.z, and e.block_id.
// - Damage events: e.entity, writable e.amount, and e.cause.
// - EntityDamageByEntity also provides e.damager.
// - Call e.cancel() to cancel cancellable events.
//
// Listeners use NORMAL priority and are removed when their plugin is disabled.
#include "LuaBindings.h"
#include "LuaPlugin.h"
#include "Player.h" // Complete Player definition.
#include "event/EventManager.h"
#include "event/player/PlayerJoinEvent.h"
#include "event/player/PlayerQuitEvent.h"
#include "event/player/PlayerChatEvent.h"
#include "event/player/PlayerDeathEvent.h"
#include "event/player/PlayerRespawnEvent.h"
#include "event/player/PlayerBedEnterEvent.h"
#include "event/player/PlayerBedLeaveEvent.h"
#include "event/player/PlayerInteractEvent.h"
#include "event/player/PlayerMoveEvent.h"
#include "event/player/PlayerDropItemEvent.h"
#include "event/player/PlayerItemHeldEvent.h"
#include "event/block/BlockBreakEvent.h"
#include "event/block/BlockPlaceEvent.h"
#include "event/block/DoorToggleEvent.h"
#include "event/entity/EntityDamageEvent.h"
#include "event/entity/EntityDamageByEntityEvent.h"
#include <functional>
#include <string>
#include <type_traits>

namespace cursestone::plugin {

namespace {

// Marks the event table as cancelled.
int l_cancel(lua_State* L) {
    lua_pushboolean(L, 1);
    lua_setfield(L, lua_upvalueindex(1), "cancelled");
    return 0;
}

// Creates the event table, calls the Lua handler, and applies writable fields.
template <typename EventT>
void hook(LuaPlugin* pl, int fnRef, std::function<void(lua_State*, int /*eIdx*/, EventT&)> fill,
          std::function<void(lua_State*, int /*eIdx*/, EventT&)> readBack) {
    event::EventManager::get().listen<EventT>(
        pl, // The plugin owns the listener.
        [pl, fnRef, fill, readBack](EventT& ev) {
            if (!pl->isEnabled())
                return;
            lua_State* L = pl->L();
            int base = lua_gettop(L);

            lua_newtable(L); // Event table.
            int eIdx = lua_gettop(L);

            // Bind cancel() to the event table.
            lua_pushvalue(L, eIdx);
            lua_pushcclosure(L, l_cancel, 1);
            lua_setfield(L, eIdx, "cancel");

            fill(L, eIdx, ev);

            // Push the handler before its argument.
            lua_rawgeti(L, LUA_REGISTRYINDEX, fnRef);
            lua_pushvalue(L, eIdx);
            if (pl->pcall(1, 0)) {
                // Apply cancellation only to cancellable event types.
                lua_getfield(L, eIdx, "cancelled");
                if (lua_toboolean(L, -1)) {
                    if constexpr (std::is_base_of_v<event::Cancellable, EventT>) {
                        ev.setCancelled(true);
                    }
                }
                lua_pop(L, 1);
                readBack(L, eIdx, ev);
            }
            lua_settop(L, base); // Restore the Lua stack.
        });
}

// Sets the player name, using an empty string for null players.
template <typename EventT>
void fillPlayer(lua_State* L, int eIdx, EventT& ev) {
    lua_pushstring(L, ev.player ? ev.player->getName().c_str() : "");
    lua_setfield(L, eIdx, "player");
}

// Applies a modified message from the event table.
template <typename EventT>
void readBackMessage(lua_State* L, int eIdx, EventT& ev) {
    lua_getfield(L, eIdx, "message");
    if (const char* s = lua_tostring(L, -1))
        ev.message = s;
    lua_pop(L, 1);
}

// Adds the player and block coordinates to the event table.
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

// Adds the item stack fields using the given prefix.
void fillItemStack(lua_State* L, int eIdx, const char* prefix,
                   const cursestone::item::ItemStack& st) {
    lua_pushfstring(L, "%s_id", prefix);
    lua_pushinteger(L, (lua_Integer)st.id);
    lua_settable(L, eIdx);
    lua_pushfstring(L, "%s_meta", prefix);
    lua_pushinteger(L, (lua_Integer)st.meta);
    lua_settable(L, eIdx);
    lua_pushfstring(L, "%s_count", prefix);
    lua_pushinteger(L, (lua_Integer)st.count);
    lua_settable(L, eIdx);
}

// Adds a readable entity identifier to the event table.
void fillEntityField(lua_State* L, int eIdx, const char* field, cursestone::entity::Entity* e) {
    if (!e) {
        lua_pushstring(L, "unknown");
        lua_setfield(L, eIdx, field);
        return;
    }
    if (auto* pl = dynamic_cast<Player*>(e)) {
        lua_pushstring(L, pl->getName().c_str());
    } else {
        lua_pushfstring(L, "eid:%lld", (long long)e->runtimeId());
    }
    lua_setfield(L, eIdx, field);
}

// Converts damage causes to lowercase event names.
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

// Adds the victim, damage amount, and cause to the event table.
void fillDamage(lua_State* L, int eIdx, event::entity::EntityDamageEvent& ev) {
    fillEntityField(L, eIdx, "entity", ev.entity);
    lua_pushnumber(L, (lua_Number)ev.amount);
    lua_setfield(L, eIdx, "amount");
    lua_pushstring(L, damageCauseName(ev.cause));
    lua_setfield(L, eIdx, "cause");
}

// Applies the modified damage amount, clamped to zero or greater.
void readBackAmount(lua_State* L, int eIdx, event::entity::EntityDamageEvent& ev) {
    lua_getfield(L, eIdx, "amount");
    if (lua_isnumber(L, -1)) {
        lua_Number a = lua_tonumber(L, -1);
        ev.amount = (float)(a > 0 ? a : 0);
    }
    lua_pop(L, 1);
}

// Adds the attacker to the damage event table.
void fillDamageByEntity(lua_State* L, int eIdx, event::entity::EntityDamageByEntityEvent& ev) {
    fillDamage(L, eIdx, ev);
    fillEntityField(L, eIdx, "damager", ev.damager);
}

// Registers a Lua event listener.
int l_events_on(lua_State* L) {
    LuaPlugin* pl = LuaPlugin::from(L);
    std::string name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    if (name == "PlayerJoin") {
        hook<event::player::PlayerJoinEvent>(pl, ref, fillPlayer<event::player::PlayerJoinEvent>,
                                             readBackMessage<event::player::PlayerJoinEvent>);
    } else if (name == "PlayerQuit") {
        hook<event::player::PlayerQuitEvent>(pl, ref, fillPlayer<event::player::PlayerQuitEvent>,
                                             readBackMessage<event::player::PlayerQuitEvent>);
    } else if (name == "PlayerChat") {
        hook<event::player::PlayerChatEvent>(pl, ref, fillPlayer<event::player::PlayerChatEvent>,
                                             readBackMessage<event::player::PlayerChatEvent>);
    } else if (name == "BlockBreak") {
        auto f = [](lua_State* L, int eIdx, event::block::BlockBreakEvent& ev) {
            fillBlock(L, eIdx, ev);
        };
        hook<event::block::BlockBreakEvent>(pl, ref, f, {});
    } else if (name == "BlockPlace") {
        auto f = [](lua_State* L, int eIdx, event::block::BlockPlaceEvent& ev) {
            fillBlock(L, eIdx, ev);
        };
        hook<event::block::BlockPlaceEvent>(pl, ref, f, {});
    } else if (name == "EntityDamage") {
        auto f = [](lua_State* L, int eIdx, event::entity::EntityDamageEvent& ev) {
            fillDamage(L, eIdx, ev);
        };
        hook<event::entity::EntityDamageEvent>(pl, ref, f, readBackAmount);
    } else if (name == "EntityDamageByEntity") {
        auto f = [](lua_State* L, int eIdx, event::entity::EntityDamageByEntityEvent& ev) {
            fillDamageByEntity(L, eIdx, ev);
        };
        // Applies writable damage and cancellation changes.
        hook<event::entity::EntityDamageByEntityEvent>(
            pl, ref, f,
            std::function<void(lua_State*, int, event::entity::EntityDamageByEntityEvent&)>(
                [](lua_State* L, int eIdx, event::entity::EntityDamageByEntityEvent& ev) {
                    readBackAmount(L, eIdx, ev);
                }));
    } else if (name == "PlayerDeath") {
        auto f = [](lua_State* L, int eIdx, event::player::PlayerDeathEvent& ev) {
            fillPlayer<event::player::PlayerDeathEvent>(L, eIdx, ev);
            lua_pushstring(L, ev.message.c_str());
            lua_setfield(L, eIdx, "message");
        };
        hook<event::player::PlayerDeathEvent>(pl, ref, f,
                                              readBackMessage<event::player::PlayerDeathEvent>);
    } else if (name == "PlayerRespawn") {
        auto f = [](lua_State* L, int eIdx, event::player::PlayerRespawnEvent& ev) {
            fillPlayer<event::player::PlayerRespawnEvent>(L, eIdx, ev);
            lua_pushnumber(L, (lua_Number)ev.respawnX);
            lua_setfield(L, eIdx, "x");
            lua_pushnumber(L, (lua_Number)ev.respawnY);
            lua_setfield(L, eIdx, "y");
            lua_pushnumber(L, (lua_Number)ev.respawnZ);
            lua_setfield(L, eIdx, "z");
        };
        // Respawn events are intentionally not cancellable.
        hook<event::player::PlayerRespawnEvent>(pl, ref, f, {});
    } else if (name == "PlayerBedEnter" || name == "PlayerSleepInBed") {
        // PlayerSleepInBed is an alias for PlayerBedEnter.
        auto f = [](lua_State* L, int eIdx, event::player::PlayerBedEnterEvent& ev) {
            fillPlayer<event::player::PlayerBedEnterEvent>(L, eIdx, ev);
            lua_pushnumber(L, (lua_Number)ev.x);
            lua_setfield(L, eIdx, "x");
            lua_pushnumber(L, (lua_Number)ev.y);
            lua_setfield(L, eIdx, "y");
            lua_pushnumber(L, (lua_Number)ev.z);
            lua_setfield(L, eIdx, "z");
        };
        hook<event::player::PlayerBedEnterEvent>(pl, ref, f, {});
    } else if (name == "PlayerBedLeave") {
        // Fired when a player stops sleeping.
        auto f = [](lua_State* L, int eIdx, event::player::PlayerBedLeaveEvent& ev) {
            fillPlayer<event::player::PlayerBedLeaveEvent>(L, eIdx, ev);
            lua_pushnumber(L, (lua_Number)ev.x);
            lua_setfield(L, eIdx, "x");
            lua_pushnumber(L, (lua_Number)ev.y);
            lua_setfield(L, eIdx, "y");
            lua_pushnumber(L, (lua_Number)ev.z);
            lua_setfield(L, eIdx, "z");
        };
        hook<event::player::PlayerBedLeaveEvent>(pl, ref, f, {});
    } else if (name == "DoorToggle") {
        // Fired when a wooden door is toggled by a player.
        auto f = [](lua_State* L, int eIdx, event::block::DoorToggleEvent& ev) {
            lua_pushstring(L, ev.player ? ev.player->getName().c_str() : "");
            lua_setfield(L, eIdx, "player");
            lua_pushnumber(L, (lua_Number)ev.x);
            lua_setfield(L, eIdx, "x");
            lua_pushnumber(L, (lua_Number)ev.y);
            lua_setfield(L, eIdx, "y");
            lua_pushnumber(L, (lua_Number)ev.z);
            lua_setfield(L, eIdx, "z");
        };
        hook<event::block::DoorToggleEvent>(pl, ref, f, {});
    } else if (name == "PlayerInteract") {
        // Fired before a block or item interaction is processed.
        auto f = [](lua_State* L, int eIdx, event::player::PlayerInteractEvent& ev) {
            fillPlayer<event::player::PlayerInteractEvent>(L, eIdx, ev);
            lua_pushnumber(L, (lua_Number)ev.blockX);
            lua_setfield(L, eIdx, "x");
            lua_pushnumber(L, (lua_Number)ev.blockY);
            lua_setfield(L, eIdx, "y");
            lua_pushnumber(L, (lua_Number)ev.blockZ);
            lua_setfield(L, eIdx, "z");
            lua_pushnumber(L, (lua_Number)ev.face);
            lua_setfield(L, eIdx, "face");
            const char* act = "left_click_block";
            if (ev.action == event::player::PlayerInteractEvent::RIGHT_CLICK_BLOCK)
                act = "right_click_block";
            else if (ev.action == event::player::PlayerInteractEvent::RIGHT_CLICK_AIR)
                act = "right_click_air";
            else if (ev.action == event::player::PlayerInteractEvent::LEFT_CLICK_AIR)
                act = "left_click_air";
            else if (ev.action == event::player::PlayerInteractEvent::PHYSICAL)
                act = "physical";
            lua_pushstring(L, act);
            lua_setfield(L, eIdx, "action");
            fillItemStack(L, eIdx, "item", ev.item);
        };
        hook<event::player::PlayerInteractEvent>(pl, ref, f, {});
    } else if (name == "PlayerMove") {
        // Fired for each accepted player movement packet.
        auto f = [](lua_State* L, int eIdx, event::player::PlayerMoveEvent& ev) {
            lua_pushstring(L, ev.player ? ev.player->getName().c_str() : "");
            lua_setfield(L, eIdx, "player");
            lua_pushnumber(L, (lua_Number)ev.fromX);
            lua_setfield(L, eIdx, "fromX");
            lua_pushnumber(L, (lua_Number)ev.fromY);
            lua_setfield(L, eIdx, "fromY");
            lua_pushnumber(L, (lua_Number)ev.fromZ);
            lua_setfield(L, eIdx, "fromZ");
            lua_pushnumber(L, (lua_Number)ev.toX);
            lua_setfield(L, eIdx, "toX");
            lua_pushnumber(L, (lua_Number)ev.toY);
            lua_setfield(L, eIdx, "toY");
            lua_pushnumber(L, (lua_Number)ev.toZ);
            lua_setfield(L, eIdx, "toZ");
        };
        hook<event::player::PlayerMoveEvent>(pl, ref, f, {});
    } else if (name == "PlayerDropItem") {
        // Fired before the item leaves the inventory.
        auto f = [](lua_State* L, int eIdx, event::player::PlayerDropItemEvent& ev) {
            lua_pushstring(L, ev.player ? ev.player->getName().c_str() : "");
            lua_setfield(L, eIdx, "player");
            fillItemStack(L, eIdx, "item", ev.item);
        };
        hook<event::player::PlayerDropItemEvent>(pl, ref, f, {});
    } else if (name == "PlayerItemHeld") {
        // Fired when the player selects a hotbar slot.
        auto f = [](lua_State* L, int eIdx, event::player::PlayerItemHeldEvent& ev) {
            lua_pushstring(L, ev.player ? ev.player->getName().c_str() : "");
            lua_setfield(L, eIdx, "player");
            fillItemStack(L, eIdx, "item", ev.item);
            lua_pushinteger(L, (lua_Integer)ev.slot);
            lua_setfield(L, eIdx, "slot");
        };
        hook<event::player::PlayerItemHeldEvent>(pl, ref, f, {});
    } else {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "unknown event: %s", name.c_str());
    }
    return 0;
}

} // namespace

void bindEvents(lua_State* L) {
    static const luaL_Reg evF[] = {{"on", l_events_on}, {nullptr, nullptr}};
    lua_newtable(L);
    luaL_setfuncs(L, evF, 0);

    // Export damage cause constants to Lua.
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