/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
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
// EntityTypes — the protocol-84 entity network id registry for the mobs
// CurseStone spawns. On this protocol the spawn egg aux value IS the
// entity network id, so one table serves the eggs, the Lua API and the
// native API: every id listed here has its own spawn egg in the 0.15.10
// creative inventory, which makes the client's renderer for it
// client-verified — ids outside the table never reach the wire.
//
// The ids follow the era reference Entity network constants (they are
// the same values the current Bedrock ActorType enum keeps for these
// mobs). 41 and 42 are BOTH magma cubes: 41 is the legacy "Lava Slime"
// id from the Nether update era, 42 the renamed id; the creative
// inventory carries an egg for each, and the client renders both.
#pragma once
#include <cstdint>
#include <string>

namespace cursestone::entity {

// Protocol-84 network ids of the supported mobs.
namespace EntityTypeId {
constexpr int32_t CHICKEN = 10;
constexpr int32_t COW = 11;
constexpr int32_t PIG = 12;
constexpr int32_t SHEEP = 13;
constexpr int32_t WOLF = 14;
constexpr int32_t VILLAGER = 15;
constexpr int32_t MOOSHROOM = 16;
constexpr int32_t SQUID = 17;
constexpr int32_t RABBIT = 18;
constexpr int32_t BAT = 19;
constexpr int32_t OCELOT = 22;
constexpr int32_t HORSE = 23;
constexpr int32_t DONKEY = 24;
constexpr int32_t MULE = 25;
constexpr int32_t SKELETON_HORSE = 26;
constexpr int32_t ZOMBIE_HORSE = 27;
constexpr int32_t ZOMBIE = 32;
constexpr int32_t CREEPER = 33;
constexpr int32_t SKELETON = 34;
constexpr int32_t SPIDER = 35;
constexpr int32_t ZOMBIE_PIGMAN = 36;
constexpr int32_t SLIME = 37;
constexpr int32_t ENDERMAN = 38;
constexpr int32_t SILVERFISH = 39;
constexpr int32_t GHAST = 40;
constexpr int32_t LAVA_SLIME = 41;
constexpr int32_t MAGMA_CUBE = 42;
constexpr int32_t BLAZE = 43;
constexpr int32_t ZOMBIE_VILLAGER = 44;
constexpr int32_t WITCH = 45;
constexpr int32_t STRAY = 46;
constexpr int32_t HUSK = 47;
constexpr int32_t WITHER_SKELETON = 48;
} // namespace EntityTypeId

// Returns true when @p id maps to a mob the 0.15.10 client renders.
bool isValidMobType(int32_t id);

// Canonical lowercase name ("chicken", "zombie_pigman", ...) for a
// registry id, or nullptr for ids outside the registry.
const char* mobTypeName(int32_t id);

// Resolves a mob type by name, case-insensitively. Accepts the canonical
// lowercase names plus the common aliases ("snowman", "mushroom_cow",
// "pigman"). Returns false and leaves @p outId untouched when unknown.
bool mobTypeFromName(const std::string& name, int32_t* outId);

} // namespace cursestone::entity
