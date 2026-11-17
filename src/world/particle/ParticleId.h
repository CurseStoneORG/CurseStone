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
// ParticleId — the wire type ids of the protocol-84 particle registry.
// The id travels in the low 12 bits of the LevelEventPacket event field
// (evid = 0x4000 | type) and the client maps it to its own renderer.
// One particle per file under world/particle/: every particle type owns
// its id, payload family, and validation in its own header.
#pragma once
#include <cstdint>

namespace cursestone::world::particle {

enum ParticleId : int32_t {
    PARTICLE_BUBBLE = 1,
    PARTICLE_CRITICAL = 2,
    PARTICLE_SMOKE = 3,
    PARTICLE_EXPLODE = 4,
    PARTICLE_WHITE_SMOKE = 5,
    PARTICLE_FLAME = 6,
    PARTICLE_LAVA = 7,
    PARTICLE_LARGE_SMOKE = 8,
    PARTICLE_REDSTONE = 9,
    PARTICLE_ITEM_BREAK = 10,
    PARTICLE_SNOWBALL_POOF = 11,
    PARTICLE_LARGE_EXPLODE = 12,
    PARTICLE_HUGE_EXPLODE = 13,
    PARTICLE_MOB_FLAME = 14,
    PARTICLE_HEART = 15,
    PARTICLE_TERRAIN = 16,
    PARTICLE_TOWN_AURA = 17,
    PARTICLE_PORTAL = 18,
    PARTICLE_WATER_SPLASH = 19,
    PARTICLE_WATER_WAKE = 20,
    PARTICLE_DRIP_WATER = 21,
    PARTICLE_DRIP_LAVA = 22,
    PARTICLE_DUST = 23,
    PARTICLE_MOB_SPELL = 24,
    PARTICLE_MOB_SPELL_AMBIENT = 25,
    PARTICLE_MOB_SPELL_INSTANT = 26,
    PARTICLE_INK = 27,
    PARTICLE_SLIME = 28,
    PARTICLE_RAIN_SPLASH = 29,
    PARTICLE_VILLAGER_ANGRY = 30,
    PARTICLE_VILLAGER_HAPPY = 31,
    PARTICLE_ENCHANTMENT_TABLE = 32,
};

/// @brief Number of particle types protocol 84 exposes (ParticleId range 1..32).
constexpr int32_t kParticleTypeCount = 32;

/// @brief True when @p id is a valid protocol-84 particle type id.
inline bool isValidParticleType(int32_t id) {
    return id >= 1 && id <= kParticleTypeCount;
}

} // namespace
