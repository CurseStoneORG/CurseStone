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

// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

// Standard C++ headers.
#include <cstddef>
#include <cstdint>
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

// Pure-C math headers (global scope; C++ wrappers would break otherwise).
#include "math/Vector2.h"
#include "math/Vector3.h"
#include "math/AxisAlignedBB.h"

// CurseStone version information.
#define CURSESTONE_API "1.0.2"
#define CURSESTONE_NAME "CurseStone"
#define CURSESTONE_CODENAME "Obsidian"

// Minecraft compatibility.
#define MINECRAFT_VERSION "0.15.x"
#define PROTOCOL_VERSION 84

// Server tick configuration.
#define CURSESTONE_TICK_RATE 20 // Ticks per second (server tick loop).
#define CURSESTONE_TICK_MS 50   // Milliseconds per tick.

// Build information.
#ifndef CURSESTONE_LEVELDB
#define CURSESTONE_LEVELDB 0
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

// World namespace.
namespace world {
class World;
class Position;
class Location;
}

// Alias for legacy code referencing cursestone::World without the
// world:: qualifier. Both refer to cursestone::world::World.
using World = world::World;

// Block namespace.
namespace block {
class Block;
}

// Item namespace.
namespace item {
class Item;
}

// Entity namespace.
namespace entity {
class Entity;
}

// Network namespace.
namespace network {
class Network;
class RakLibInterface;
class SourceInterface;

namespace protocol {
class DataPacket;
class BatchPacket;
}
}

// Inventory namespace.
namespace inventory {
class Inventory;
class PlayerInventory;
}

// Tile namespace.
namespace tile {
class Tile;
}

// NBT namespace.
namespace nbt {
class NBT;

namespace tag {
class Tag;
class CompoundTag;
}
}

// Math namespace.
namespace math {
// C++ namespace aliases for the C struct types defined in the
// global-scope headers above.
using Vector2 = ::mcp_vector2_t;
using Vector3 = ::mcp_vector3_t;
using AxisAlignedBB = ::mcp_aabb_t;
}

// Utilities namespace.
namespace utils {
class BinaryStream;
class Config;
class MainLogger;
class UUID;
}

// Scheduler namespace.
namespace scheduler {
class ServerScheduler;
class Task;
class AsyncTask;
}

// Permission namespace.
namespace permission {
class Permission;
class Permissible;
}

// Command namespace.
namespace command {
class Command;
class CommandSender;
class CommandMap;
}

// Event namespace.
namespace event {
class Event;
class HandlerList;
}

} // namespace cursestone