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
// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityTypes — the protocol-84 entity network id registry for the mobs
// CurseStone spawns. On this protocol the spawn egg aux value IS the
// entity network id, so one table serves the eggs, the Lua API and the
// native API: every id listed here has its own spawn egg in the 0.15.10
// creative inventory, which makes the client's renderer for it
// client-verified — ids outside the table never reach the wire.
//
// The id space itself lives one header per mob in mobs/ (MobList.h is the
// index), mirroring the particle split: a mob's file is the single
// authority for its id, name, aliases and kind, and this registry
// aggregates them. The EntityTypeId constants below are stable aliases so
// existing callers keep spelling ids as EntityTypeId::CHICKEN.
//
// The ids follow the era reference Entity network constants (they are
// the same values the current Bedrock ActorType enum keeps for these
// mobs). 41 and 42 are BOTH magma cubes: 41 is the legacy "Lava Slime"
// id from the Nether update era, 42 the renamed id; the creative
// inventory carries an egg for each, and the client renders both.
#pragma once
#include "mobs/MobList.h"
#include <cstdint>
#include <string>

namespace cursestone::entity {

// Protocol-84 network ids of the supported mobs, aliased from the per-mob
// headers in mobs/.
namespace EntityTypeId {
constexpr int32_t CHICKEN = mobs::Chicken::kId;
constexpr int32_t COW = mobs::Cow::kId;
constexpr int32_t PIG = mobs::Pig::kId;
constexpr int32_t SHEEP = mobs::Sheep::kId;
constexpr int32_t WOLF = mobs::Wolf::kId;
constexpr int32_t VILLAGER = mobs::Villager::kId;
constexpr int32_t MOOSHROOM = mobs::Mooshroom::kId;
constexpr int32_t SQUID = mobs::Squid::kId;
constexpr int32_t RABBIT = mobs::Rabbit::kId;
constexpr int32_t BAT = mobs::Bat::kId;
constexpr int32_t OCELOT = mobs::Ocelot::kId;
constexpr int32_t HORSE = mobs::Horse::kId;
constexpr int32_t DONKEY = mobs::Donkey::kId;
constexpr int32_t MULE = mobs::Mule::kId;
constexpr int32_t SKELETON_HORSE = mobs::SkeletonHorse::kId;
constexpr int32_t ZOMBIE_HORSE = mobs::ZombieHorse::kId;
constexpr int32_t ZOMBIE = mobs::Zombie::kId;
constexpr int32_t CREEPER = mobs::Creeper::kId;
constexpr int32_t SKELETON = mobs::Skeleton::kId;
constexpr int32_t SPIDER = mobs::Spider::kId;
constexpr int32_t ZOMBIE_PIGMAN = mobs::ZombiePigman::kId;
constexpr int32_t SLIME = mobs::Slime::kId;
constexpr int32_t ENDERMAN = mobs::Enderman::kId;
constexpr int32_t SILVERFISH = mobs::Silverfish::kId;
constexpr int32_t GHAST = mobs::Ghast::kId;
constexpr int32_t LAVA_SLIME = mobs::LavaSlime::kId;
constexpr int32_t MAGMA_CUBE = mobs::MagmaCube::kId;
constexpr int32_t BLAZE = mobs::Blaze::kId;
constexpr int32_t ZOMBIE_VILLAGER = mobs::ZombieVillager::kId;
constexpr int32_t WITCH = mobs::Witch::kId;
constexpr int32_t STRAY = mobs::Stray::kId;
constexpr int32_t HUSK = mobs::Husk::kId;
constexpr int32_t WITHER_SKELETON = mobs::WitherSkeleton::kId;
} // namespace EntityTypeId

/// @brief The behavior family of a registered mob (mobs/MobKind.h).
using MobKind = mobs::MobKind;

/// @brief Returns true when @p id maps to a mob the 0.15.10 client renders.
///
/// O(1): the registry ids are direct-indexed, not scanned.
bool isValidMobType(int32_t id);

/// @brief Canonical lowercase name ("chicken", "zombie_pigman", ...) for a
///        registry id, or nullptr for ids outside the registry.
///
/// O(1): the registry ids are direct-indexed, not scanned.
const char* mobTypeName(int32_t id);

/// @brief Resolves a mob type by name, case-insensitively. Accepts the canonical
///        lowercase names plus the common aliases ("mushroom_cow", "pigman",
///        "magma", "lava_cube", "cat"). Returns false and leaves @p outId
///        untouched when unknown.
///
/// Lookup is a binary search over a compile-time sorted name index.
///
/// @param name  candidate mob name, matched case-insensitively.
/// @param outId receives the registry id on success, untouched on failure.
/// @return true when @p name resolves to a registered mob.
bool mobTypeFromName(const std::string& name, int32_t* outId);

/// @brief Behavior family of a registry id, or MobKind::Unknown for ids
///        outside the registry.
///
/// O(1): the registry ids are direct-indexed, not scanned.
MobKind mobTypeKind(int32_t id);

} // namespace cursestone::entity
