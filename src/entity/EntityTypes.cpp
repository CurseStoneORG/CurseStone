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
// EntityTypes — the runtime side of the protocol-84 mob registry. Every
// row reads exactly one mob's own header (mobs/MobList.h), so adding a
// mob is one header plus one row; the lookup tables below are built at
// compile time, which keeps every registry query O(1) with zero runtime
// initialization.
#include "EntityTypes.h"
#include <algorithm>
#include <array>
#include <cctype>

namespace cursestone::entity {
namespace {

// One row per client-verified network id, in registry id order. The list
// reads like the era reference Entity network constants.
struct MobRow {
    int32_t id;
    const char* name; // Canonical lowercase name.
    MobKind kind;
    const char* const* aliases; // nullptr when the mob has none.
    size_t aliasCount;
};

constexpr MobRow kMobTypes[] = {
    {mobs::Chicken::kId, mobs::Chicken::kName, mobs::Chicken::kKind,
     mobs::Chicken::kAliases, mobs::Chicken::kAliasCount},
    {mobs::Cow::kId, mobs::Cow::kName, mobs::Cow::kKind,
     mobs::Cow::kAliases, mobs::Cow::kAliasCount},
    {mobs::Pig::kId, mobs::Pig::kName, mobs::Pig::kKind,
     mobs::Pig::kAliases, mobs::Pig::kAliasCount},
    {mobs::Sheep::kId, mobs::Sheep::kName, mobs::Sheep::kKind,
     mobs::Sheep::kAliases, mobs::Sheep::kAliasCount},
    {mobs::Wolf::kId, mobs::Wolf::kName, mobs::Wolf::kKind,
     mobs::Wolf::kAliases, mobs::Wolf::kAliasCount},
    {mobs::Villager::kId, mobs::Villager::kName, mobs::Villager::kKind,
     mobs::Villager::kAliases, mobs::Villager::kAliasCount},
    {mobs::Mooshroom::kId, mobs::Mooshroom::kName, mobs::Mooshroom::kKind,
     mobs::Mooshroom::kAliases, mobs::Mooshroom::kAliasCount},
    {mobs::Squid::kId, mobs::Squid::kName, mobs::Squid::kKind,
     mobs::Squid::kAliases, mobs::Squid::kAliasCount},
    {mobs::Rabbit::kId, mobs::Rabbit::kName, mobs::Rabbit::kKind,
     mobs::Rabbit::kAliases, mobs::Rabbit::kAliasCount},
    {mobs::Bat::kId, mobs::Bat::kName, mobs::Bat::kKind,
     mobs::Bat::kAliases, mobs::Bat::kAliasCount},
    {mobs::Ocelot::kId, mobs::Ocelot::kName, mobs::Ocelot::kKind,
     mobs::Ocelot::kAliases, mobs::Ocelot::kAliasCount},
    {mobs::Horse::kId, mobs::Horse::kName, mobs::Horse::kKind,
     mobs::Horse::kAliases, mobs::Horse::kAliasCount},
    {mobs::Donkey::kId, mobs::Donkey::kName, mobs::Donkey::kKind,
     mobs::Donkey::kAliases, mobs::Donkey::kAliasCount},
    {mobs::Mule::kId, mobs::Mule::kName, mobs::Mule::kKind,
     mobs::Mule::kAliases, mobs::Mule::kAliasCount},
    {mobs::SkeletonHorse::kId, mobs::SkeletonHorse::kName, mobs::SkeletonHorse::kKind,
     mobs::SkeletonHorse::kAliases, mobs::SkeletonHorse::kAliasCount},
    {mobs::ZombieHorse::kId, mobs::ZombieHorse::kName, mobs::ZombieHorse::kKind,
     mobs::ZombieHorse::kAliases, mobs::ZombieHorse::kAliasCount},
    {mobs::Zombie::kId, mobs::Zombie::kName, mobs::Zombie::kKind,
     mobs::Zombie::kAliases, mobs::Zombie::kAliasCount},
    {mobs::Creeper::kId, mobs::Creeper::kName, mobs::Creeper::kKind,
     mobs::Creeper::kAliases, mobs::Creeper::kAliasCount},
    {mobs::Skeleton::kId, mobs::Skeleton::kName, mobs::Skeleton::kKind,
     mobs::Skeleton::kAliases, mobs::Skeleton::kAliasCount},
    {mobs::Spider::kId, mobs::Spider::kName, mobs::Spider::kKind,
     mobs::Spider::kAliases, mobs::Spider::kAliasCount},
    {mobs::ZombiePigman::kId, mobs::ZombiePigman::kName, mobs::ZombiePigman::kKind,
     mobs::ZombiePigman::kAliases, mobs::ZombiePigman::kAliasCount},
    {mobs::Slime::kId, mobs::Slime::kName, mobs::Slime::kKind,
     mobs::Slime::kAliases, mobs::Slime::kAliasCount},
    {mobs::Enderman::kId, mobs::Enderman::kName, mobs::Enderman::kKind,
     mobs::Enderman::kAliases, mobs::Enderman::kAliasCount},
    {mobs::Silverfish::kId, mobs::Silverfish::kName, mobs::Silverfish::kKind,
     mobs::Silverfish::kAliases, mobs::Silverfish::kAliasCount},
    {mobs::Ghast::kId, mobs::Ghast::kName, mobs::Ghast::kKind,
     mobs::Ghast::kAliases, mobs::Ghast::kAliasCount},
    {mobs::LavaSlime::kId, mobs::LavaSlime::kName, mobs::LavaSlime::kKind,
     mobs::LavaSlime::kAliases, mobs::LavaSlime::kAliasCount},
    {mobs::MagmaCube::kId, mobs::MagmaCube::kName, mobs::MagmaCube::kKind,
     mobs::MagmaCube::kAliases, mobs::MagmaCube::kAliasCount},
    {mobs::Blaze::kId, mobs::Blaze::kName, mobs::Blaze::kKind,
     mobs::Blaze::kAliases, mobs::Blaze::kAliasCount},
    {mobs::ZombieVillager::kId, mobs::ZombieVillager::kName, mobs::ZombieVillager::kKind,
     mobs::ZombieVillager::kAliases, mobs::ZombieVillager::kAliasCount},
    {mobs::Witch::kId, mobs::Witch::kName, mobs::Witch::kKind,
     mobs::Witch::kAliases, mobs::Witch::kAliasCount},
    {mobs::Stray::kId, mobs::Stray::kName, mobs::Stray::kKind,
     mobs::Stray::kAliases, mobs::Stray::kAliasCount},
    {mobs::Husk::kId, mobs::Husk::kName, mobs::Husk::kKind,
     mobs::Husk::kAliases, mobs::Husk::kAliasCount},
    {mobs::WitherSkeleton::kId, mobs::WitherSkeleton::kName, mobs::WitherSkeleton::kKind,
     mobs::WitherSkeleton::kAliases, mobs::WitherSkeleton::kAliasCount},
};

constexpr size_t kMobTypeCount = sizeof(kMobTypes) / sizeof(kMobTypes[0]);

// Vanilla maximum health per row, same order as kMobTypes (the era
// reference Living health table; slimes and magma cubes read the small
// variant's 4, horses the family's average 20).
constexpr float kMobMaxHealth[] = {
    4.0f,   // Chicken
    10.0f,  // Cow
    10.0f,  // Pig
    8.0f,   // Sheep
    8.0f,   // Wolf
    20.0f,  // Villager
    10.0f,  // Mooshroom
    10.0f,  // Squid
    3.0f,   // Rabbit
    6.0f,   // Bat
    10.0f,  // Ocelot
    20.0f,  // Horse
    20.0f,  // Donkey
    20.0f,  // Mule
    15.0f,  // SkeletonHorse
    15.0f,  // ZombieHorse
    20.0f,  // Zombie
    20.0f,  // Creeper
    20.0f,  // Skeleton
    16.0f,  // Spider
    20.0f,  // ZombiePigman
    4.0f,   // Slime
    40.0f,  // Enderman
    8.0f,   // Silverfish
    10.0f,  // Ghast
    4.0f,   // LavaSlime
    4.0f,   // MagmaCube
    20.0f,  // Blaze
    20.0f,  // ZombieVillager
    26.0f,  // Witch
    20.0f,  // Stray
    20.0f,  // Husk
    20.0f,  // WitherSkeleton
};
static_assert(sizeof(kMobMaxHealth) / sizeof(kMobMaxHealth[0]) == kMobTypeCount,
              "kMobMaxHealth must have one row per kMobTypes entry");

// Per-species movement and body profile, same row order as kMobTypes.
// Speeds are blocks per tick: the walk column is the calm stroll the
// wander goal drives (vanilla wandering runs at the mob's own walk
// speed — roughly twice the flat 0.055 pace mobs used before species
// profiles existed), the panic column the flee pace of the panic goal.
// Sizes are the era collision boxes the actuation sweeps against, and
// the eye heights are the look-pitch pivots of the head tracking.
// grounded=false keeps a kind static (pre-AI record): flight (ghast,
// blaze, bat), hopping (slimes) and swimming (squid) move on models
// this stage does not implement.
constexpr MobProfile kMobProfiles[] = {
    {0.075f, 0.17f, 0.4f, 0.7f, 0.65f, true},   // Chicken (darty scurry)
    {0.090f, 0.20f, 0.9f, 1.4f, 1.30f, true},   // Cow (heavy trot)
    {0.095f, 0.21f, 0.9f, 0.9f, 0.85f, true},   // Pig (sniffing trot)
    {0.085f, 0.19f, 0.9f, 1.3f, 1.20f, true},   // Sheep (docile stroll)
    {0.090f, 0.26f, 0.6f, 0.85f, 0.75f, true},  // Wolf (lean lope)
    {0.085f, 0.19f, 0.6f, 1.95f, 1.65f, true},  // Villager (hurried walk)
    {0.090f, 0.20f, 0.9f, 1.4f, 1.30f, true},   // Mooshroom (= cow)
    {0.000f, 0.00f, 0.8f, 0.8f, 0.40f, false},  // Squid (aquatic, static)
    {0.100f, 0.32f, 0.4f, 0.5f, 0.45f, true},   // Rabbit (startle bursts)
    {0.000f, 0.00f, 0.5f, 0.9f, 0.80f, false},  // Bat (flier, static)
    {0.095f, 0.24f, 0.6f, 0.7f, 0.65f, true},   // Ocelot (cat strides)
    {0.100f, 0.22f, 1.4f, 1.6f, 1.40f, true},   // Horse
    {0.095f, 0.21f, 1.4f, 1.6f, 1.40f, true},   // Donkey
    {0.095f, 0.21f, 1.4f, 1.6f, 1.40f, true},   // Mule
    {0.100f, 0.22f, 1.4f, 1.6f, 1.40f, true},   // SkeletonHorse
    {0.095f, 0.21f, 1.4f, 1.6f, 1.40f, true},   // ZombieHorse
    {0.085f, 0.00f, 0.6f, 1.95f, 1.65f, true},  // Zombie (shambling prowl)
    {0.090f, 0.00f, 0.6f, 1.7f, 1.45f, true},   // Creeper (silent stalk)
    {0.090f, 0.00f, 0.6f, 1.99f, 1.69f, true},  // Skeleton
    {0.110f, 0.00f, 1.4f, 0.9f, 0.75f, true},   // Spider (skittering)
    {0.085f, 0.00f, 0.6f, 1.95f, 1.65f, true},  // ZombiePigman
    {0.000f, 0.00f, 0.8f, 0.8f, 0.40f, false},  // Slime (hops, static)
    {0.090f, 0.00f, 0.6f, 2.9f, 2.55f, true},   // Enderman
    {0.090f, 0.00f, 0.4f, 0.3f, 0.25f, true},   // Silverfish
    {0.000f, 0.00f, 4.0f, 4.0f, 1.50f, false},  // Ghast (flier, static)
    {0.000f, 0.00f, 0.8f, 0.8f, 0.40f, false},  // LavaSlime (hops, static)
    {0.000f, 0.00f, 0.8f, 0.8f, 0.40f, false},  // MagmaCube (hops, static)
    {0.000f, 0.00f, 0.6f, 1.8f, 1.60f, false},  // Blaze (hover, static)
    {0.085f, 0.00f, 0.6f, 1.95f, 1.65f, true},  // ZombieVillager
    {0.085f, 0.00f, 0.6f, 1.95f, 1.65f, true},  // Witch
    {0.090f, 0.00f, 0.6f, 1.99f, 1.69f, true},  // Stray
    {0.085f, 0.00f, 0.6f, 1.95f, 1.65f, true},  // Husk
    {0.090f, 0.00f, 0.7f, 2.4f, 2.10f, true},   // WitherSkeleton
};
static_assert(sizeof(kMobProfiles) / sizeof(kMobProfiles[0]) == kMobTypeCount,
              "kMobProfiles must have one row per kMobTypes entry");

// Registry ids span [kMinId, kMaxId]; the direct index covers the span.
constexpr int32_t kMinId = kMobTypes[0].id;
constexpr int32_t kMaxId = kMobTypes[kMobTypeCount - 1].id;
constexpr size_t kIdSpan = (size_t)(kMaxId - kMinId) + 1;

// slot -> row index, or -1 when the id is not registered. Built by the
// compiler: a lookup is one array read instead of a table scan.
constexpr std::array<int8_t, kIdSpan> kRowBySlot = [] {
    std::array<int8_t, kIdSpan> t{};
    t.fill(-1);
    for (size_t i = 0; i < kMobTypeCount; ++i)
        t[kMobTypes[i].id - kMinId] = (int8_t)i;
    return t;
}();

// Constexpr stand-in for strcmp (strcmp itself is not constexpr).
constexpr int cstrCompare(const char* a, const char* b) {
    for (size_t i = 0;; ++i) {
        const unsigned char ca = (unsigned char)a[i];
        const unsigned char cb = (unsigned char)b[i];
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (ca == 0)
            return 0;
    }
}

struct NameEntry {
    const char* name; // Canonical name or alias.
    int32_t id;
};

constexpr size_t kNameCount = [] {
    size_t n = 0;
    for (const MobRow& r : kMobTypes)
        n += 1 + r.aliasCount;
    return n;
}();

// Canonical names plus aliases, sorted at compile time so name lookup is
// a binary search with no runtime initialization.
constexpr std::array<NameEntry, kNameCount> kNameIndex = [] {
    std::array<NameEntry, kNameCount> t{};
    size_t n = 0;
    for (const MobRow& r : kMobTypes) {
        t[n++] = {r.name, r.id};
        for (size_t i = 0; i < r.aliasCount; ++i)
            t[n++] = {r.aliases[i], r.id};
    }
    std::sort(t.begin(), t.end(), [](const NameEntry& a, const NameEntry& b) {
        return cstrCompare(a.name, b.name) < 0;
    });
    return t;
}();

// Lowercases an ASCII string in place of a copy.
std::string asciiLower(const std::string& in) {
    std::string out = in;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// Slot of @p id, or -1 when the id is outside the registry.
int8_t rowSlot(int32_t id) {
    if (id < kMinId || id > kMaxId)
        return -1;
    return kRowBySlot[id - kMinId];
}

} // namespace

bool isValidMobType(int32_t id) {
    return rowSlot(id) >= 0;
}

const char* mobTypeName(int32_t id) {
    const int8_t slot = rowSlot(id);
    return slot >= 0 ? kMobTypes[slot].name : nullptr;
}

bool mobTypeFromName(const std::string& name, int32_t* outId) {
    if (name.empty() || !outId)
        return false;
    const std::string key = asciiLower(name);

    const auto it = std::lower_bound(
        kNameIndex.begin(), kNameIndex.end(), key,
        [](const NameEntry& e, const std::string& k) {
            return cstrCompare(e.name, k.c_str()) < 0;
        });
    if (it == kNameIndex.end() || cstrCompare(it->name, key.c_str()) != 0)
        return false;
    *outId = it->id;
    return true;
}

MobKind mobTypeKind(int32_t id) {
    const int8_t slot = rowSlot(id);
    return slot >= 0 ? kMobTypes[slot].kind : MobKind::Unknown;
}

float mobMaxHealth(int32_t id) {
    const int8_t slot = rowSlot(id);
    return slot >= 0 ? kMobMaxHealth[slot] : 0.0f;
}

MobProfile mobProfile(int32_t id) {
    const int8_t slot = rowSlot(id);
    if (slot < 0)
        return MobProfile{}; // Zero profile: static kind, never moves.
    return kMobProfiles[slot];
}

} // namespace cursestone::entity
