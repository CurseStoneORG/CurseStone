/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___/

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
// ParticleNames.cpp — name table for the protocol-84 particle registry.
// The order and ids mirror ParticleId exactly; both plugin runtimes resolve
// names through this table so Lua and native plugins never drift apart.
#include "ParticleNames.h"
#include "Particle.h"

namespace cursestone::world::particle {
namespace {

struct ParticleName {
    const char* name;
    int32_t id;
};

// Names must stay in sync with ParticleId: same ids, one row per type.
constexpr ParticleName kParticleNames[] = {
    {"bubble", PARTICLE_BUBBLE},
    {"critical", PARTICLE_CRITICAL},
    {"smoke", PARTICLE_SMOKE},
    {"explode", PARTICLE_EXPLODE},
    {"white_smoke", PARTICLE_WHITE_SMOKE},
    {"flame", PARTICLE_FLAME},
    {"lava", PARTICLE_LAVA},
    {"large_smoke", PARTICLE_LARGE_SMOKE},
    {"redstone", PARTICLE_REDSTONE},
    {"item_break", PARTICLE_ITEM_BREAK},
    {"snowball_poof", PARTICLE_SNOWBALL_POOF},
    {"large_explode", PARTICLE_LARGE_EXPLODE},
    {"huge_explode", PARTICLE_HUGE_EXPLODE},
    {"mob_flame", PARTICLE_MOB_FLAME},
    {"heart", PARTICLE_HEART},
    {"terrain", PARTICLE_TERRAIN},
    {"town_aura", PARTICLE_TOWN_AURA},
    {"portal", PARTICLE_PORTAL},
    {"water_splash", PARTICLE_WATER_SPLASH},
    {"water_wake", PARTICLE_WATER_WAKE},
    {"drip_water", PARTICLE_DRIP_WATER},
    {"drip_lava", PARTICLE_DRIP_LAVA},
    {"dust", PARTICLE_DUST},
    {"mob_spell", PARTICLE_MOB_SPELL},
    {"mob_spell_ambient", PARTICLE_MOB_SPELL_AMBIENT},
    {"mob_spell_instant", PARTICLE_MOB_SPELL_INSTANT},
    {"ink", PARTICLE_INK},
    {"slime", PARTICLE_SLIME},
    {"rain_splash", PARTICLE_RAIN_SPLASH},
    {"villager_angry", PARTICLE_VILLAGER_ANGRY},
    {"villager_happy", PARTICLE_VILLAGER_HAPPY},
    {"enchantment_table", PARTICLE_ENCHANTMENT_TABLE},
};

} // namespace

bool particleFromName(std::string_view name, int32_t& out_id) {
    for (const auto& row : kParticleNames) {
        if (name == row.name) {
            out_id = row.id;
            return true;
        }
    }
    return false;
}

} // namespace cursestone::world::particle
