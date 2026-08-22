// SPDX-License-Identifier: LGPL-3.0-or-later
// MCPPEServer — Minecraft Bedrock server software in C/C++.
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
#define MCPPESERVER_VERSION    "1.0.0"
#define MCPPESERVER_NAME      "MCPPEServer"
#define MCPPESERVER_CODENAME  "Obsidian"

// ---- Target Minecraft version ----
// MCPPEServer targets MCPE/Bedrock protocol version 84 (MCPE 0.15.x era).
#define MINECRAFT_VERSION      "0.15.x"
#define PROTOCOL_VERSION       84

// ---- Tick loop tuning ----
#define TICK_RATE              20    // ticks per second
#define TICK_MS                50    // milliseconds per tick

// ---- Build info ----
#ifndef MCPPESERVER_LEVELDB
#  define MCPPESERVER_LEVELDB 0
#endif

namespace mcppeserver {

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

namespace level       { class Level; class Position; class Location; }
// Alias so legacy code referencing `mcppeserver::Level` (without the level::
// qualifier) still works. Both refer to mcppeserver::level::Level.
using Level = level::Level;
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

} // namespace mcppeserver
