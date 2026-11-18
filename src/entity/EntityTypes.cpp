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
#include "EntityTypes.h"
#include <cctype>

namespace cursestone::entity {
namespace {

struct MobTypeDesc {
    int32_t id;
    const char* name; // Canonical lowercase name.
};

// One row per client-verified network id (sorted by id). The list reads
// like the era reference Entity network constants.
constexpr MobTypeDesc kMobTypes[] = {
    {EntityTypeId::CHICKEN, "chicken"},
    {EntityTypeId::COW, "cow"},
    {EntityTypeId::PIG, "pig"},
    {EntityTypeId::SHEEP, "sheep"},
    {EntityTypeId::WOLF, "wolf"},
    {EntityTypeId::VILLAGER, "villager"},
    {EntityTypeId::MOOSHROOM, "mooshroom"},
    {EntityTypeId::SQUID, "squid"},
    {EntityTypeId::RABBIT, "rabbit"},
    {EntityTypeId::BAT, "bat"},
    {EntityTypeId::OCELOT, "ocelot"},
    {EntityTypeId::HORSE, "horse"},
    {EntityTypeId::DONKEY, "donkey"},
    {EntityTypeId::MULE, "mule"},
    {EntityTypeId::SKELETON_HORSE, "skeleton_horse"},
    {EntityTypeId::ZOMBIE_HORSE, "zombie_horse"},
    {EntityTypeId::ZOMBIE, "zombie"},
    {EntityTypeId::CREEPER, "creeper"},
    {EntityTypeId::SKELETON, "skeleton"},
    {EntityTypeId::SPIDER, "spider"},
    {EntityTypeId::ZOMBIE_PIGMAN, "zombie_pigman"},
    {EntityTypeId::SLIME, "slime"},
    {EntityTypeId::ENDERMAN, "enderman"},
    {EntityTypeId::SILVERFISH, "silverfish"},
    {EntityTypeId::GHAST, "ghast"},
    {EntityTypeId::LAVA_SLIME, "lava_slime"},
    {EntityTypeId::MAGMA_CUBE, "magma_cube"},
    {EntityTypeId::BLAZE, "blaze"},
    {EntityTypeId::ZOMBIE_VILLAGER, "zombie_villager"},
    {EntityTypeId::WITCH, "witch"},
    {EntityTypeId::STRAY, "stray"},
    {EntityTypeId::HUSK, "husk"},
    {EntityTypeId::WITHER_SKELETON, "wither_skeleton"},
};

constexpr size_t kMobTypeCount = sizeof(kMobTypes) / sizeof(kMobTypes[0]);

// Common aliases: old or colloquial names that map onto registry ids.
struct MobTypeAlias {
    const char* name;
    int32_t id;
};

constexpr MobTypeAlias kMobTypeAliases[] = {
    {"mushroom_cow", EntityTypeId::MOOSHROOM},
    {"pigman", EntityTypeId::ZOMBIE_PIGMAN},
    {"zombie_pig", EntityTypeId::ZOMBIE_PIGMAN},
    {"magma", EntityTypeId::MAGMA_CUBE},
    {"lava_cube", EntityTypeId::LAVA_SLIME},
    {"cat", EntityTypeId::OCELOT},
};

constexpr size_t kMobAliasCount = sizeof(kMobTypeAliases) / sizeof(kMobTypeAliases[0]);

// Lowercases an ASCII string in place of a copy.
std::string asciiLower(const std::string& in) {
    std::string out = in;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

} // namespace

bool isValidMobType(int32_t id) {
    for (size_t i = 0; i < kMobTypeCount; ++i) {
        if (kMobTypes[i].id == id)
            return true;
        if (kMobTypes[i].id > id)
            break; // Sorted by id: nothing later can match.
    }
    return false;
}

const char* mobTypeName(int32_t id) {
    for (size_t i = 0; i < kMobTypeCount; ++i) {
        if (kMobTypes[i].id == id)
            return kMobTypes[i].name;
        if (kMobTypes[i].id > id)
            break;
    }
    return nullptr;
}

bool mobTypeFromName(const std::string& name, int32_t* outId) {
    if (name.empty() || !outId)
        return false;
    const std::string key = asciiLower(name);

    for (size_t i = 0; i < kMobTypeCount; ++i) {
        if (key == kMobTypes[i].name) {
            *outId = kMobTypes[i].id;
            return true;
        }
    }
    for (size_t i = 0; i < kMobAliasCount; ++i) {
        if (key == kMobTypeAliases[i].name) {
            *outId = kMobTypeAliases[i].id;
            return true;
        }
    }
    return false;
}

} // namespace cursestone::entity
