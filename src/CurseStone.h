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
// SPDX-License-Identifier: LGPL-3.0-or-later
// CurseStone — umbrella header for the server core: version constants,
// tick tuning, build flags, and common forward declarations.
// Inspired by PocketMine-MP 4.0.0; mirrors its module layout.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <map>
#include <set>
#include <optional>
#include <functional>
#include <atomic>
#include <mutex>
#include <chrono>

// ---- Pure-C math headers (global scope; C++ wrappers would break otherwise) ----
#include "math/Vector2.h"
#include "math/Vector3.h"
#include "math/AxisAlignedBB.h"

// ---- Project version constants ----
#define CURSESTONE_VERSION    "1.0.1" // server version; same value as CURSESTONE_API_VERSION for this release
#define CURSESTONE_NAME      "CurseStone"
#define CURSESTONE_CODENAME  "Obsidian"

// ---- Plugin API version ----
// API exposed to Lua plugins (plugin.yml `api` field). Mirrored by
// plugin::PLUGIN_API_VERSION in plugin/LuaBindings.h.
#define CURSESTONE_API_VERSION "1.0.1"

// ---- Target Minecraft version ----
// CurseStone targets MCPE/Bedrock protocol version 84 (MCPE 0.15.x era).
#define MINECRAFT_VERSION      "0.15.x"
#define PROTOCOL_VERSION       84

// ---- Tick loop tuning ----
// NOTE: The macro is prefixed with CURSESTONE_ to avoid colliding with the
// `static constexpr int TICK_RATE = 30;` member declared in
// block/BlockFire.h (fire spreading tick rate). Without the prefix,
// the preprocessor would rewrite that member into `static constexpr int 20 = 30;`
// which is a hard compile error on both GCC and Clang.
#define CURSESTONE_TICK_RATE          20    // ticks per second (server tick loop)
#define CURSESTONE_TICK_MS            50    // milliseconds per tick
// Backwards-compat alias (deprecated) — kept so external code that still
// references TICK_RATE keeps compiling. New code should use CURSESTONE_TICK_RATE.
// We deliberately do NOT define the bare `TICK_RATE` token here, because
// doing so would re-introduce the macro collision with BlockFire::TICK_RATE.

// ---- Build info ----
#ifndef CURSESTONE_LEVELDB
#  define CURSESTONE_LEVELDB 0
#endif

namespace cursestone {

// Common forward declarations to break circular dependencies.
class Server;
class Player;
class IPlayer;
class OfflinePlayer;
class Block;
class Item;
class Entity;
class Network;
class CommandMap;
class ServerScheduler;
class MainLogger;

namespace world       { class World; class Position; class Location; }
// Alias so legacy code referencing `cursestone::World` (without the world::
// qualifier) still works. Both refer to cursestone::world::World.
using World = world::World;
namespace block       { class Block; }
namespace item        { class Item; }
namespace entity      { class Entity; }
namespace network     { class Network; class RakLibInterface; class SourceInterface;
                        namespace protocol { class DataPacket; class BatchPacket; } }
namespace inventory   { class Inventory; class PlayerInventory; }
namespace tile        { class Tile; }
namespace nbt         { class NBT; namespace tag { class Tag; class CompoundTag; } }
namespace math        {
    // C++ namespace aliases for the C struct types defined in the
    // global-scope headers above.
    using Vector2       = ::mcp_vector2_t;
    using Vector3       = ::mcp_vector3_t;
    using AxisAlignedBB = ::mcp_aabb_t;
}
namespace utils       { class BinaryStream; class Config; class MainLogger; class UUID; }
namespace scheduler   { class ServerScheduler; class Task; class AsyncTask; }
namespace permission  { class Permission; class Permissible; }
namespace command     { class Command; class CommandSender; class CommandMap; }
namespace event       { class Event; class HandlerList; }

} // namespace cursestone
