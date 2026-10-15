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
// Particle — abstract level particle. Particles are sent to players
// in range via LevelEventPacket (evid = EVENT_ADD_PARTICLE_MASK | type).
// The server-side particle object only computes position and forwards it
// to the network layer.
#pragma once
#include "world/Position.h"
#include <cstdint>
namespace cursestone::world::particle {

// Particle types of protocol 84. The id travels in the low 12 bits of the
// LevelEventPacket event field (0x4000 | type) and the client maps it to
// its own particle renderer.
enum ParticleId : int32_t {
    PARTICLE_BUBBLE            = 1,
    PARTICLE_CRITICAL          = 2,
    PARTICLE_SMOKE             = 3,
    PARTICLE_EXPLODE           = 4,
    PARTICLE_WHITE_SMOKE       = 5,
    PARTICLE_FLAME             = 6,
    PARTICLE_LAVA              = 7,
    PARTICLE_LARGE_SMOKE       = 8,
    PARTICLE_REDSTONE          = 9,
    PARTICLE_ITEM_BREAK        = 10,
    PARTICLE_SNOWBALL_POOF     = 11,
    PARTICLE_LARGE_EXPLODE     = 12,
    PARTICLE_HUGE_EXPLODE      = 13,
    PARTICLE_MOB_FLAME         = 14,
    PARTICLE_HEART             = 15,
    PARTICLE_TERRAIN           = 16,
    PARTICLE_TOWN_AURA         = 17,
    PARTICLE_PORTAL            = 18,
    PARTICLE_WATER_SPLASH      = 19,
    PARTICLE_WATER_WAKE        = 20,
    PARTICLE_DRIP_WATER        = 21,
    PARTICLE_DRIP_LAVA         = 22,
    PARTICLE_DUST              = 23,
    PARTICLE_MOB_SPELL         = 24,
    PARTICLE_MOB_SPELL_AMBIENT = 25,
    PARTICLE_MOB_SPELL_INSTANT = 26,
    PARTICLE_INK               = 27,
    PARTICLE_SLIME             = 28,
    PARTICLE_RAIN_SPLASH       = 29,
    PARTICLE_VILLAGER_ANGRY    = 30,
    PARTICLE_VILLAGER_HAPPY    = 31,
    PARTICLE_ENCHANTMENT_TABLE = 32,
};
class Particle {
public:
    // data = event payload (particle scale / color), protocol 84.
    Particle(ParticleId id, Position pos, int32_t data = 0)
        : id_(id), pos_(pos), data_(data) {}
    ParticleId id() const { return id_; }
    const Position& position() const { return pos_; }
    // Wire type id (low 12 bits of the LevelEventPacket event field).
    virtual int32_t networkId() const { return (int32_t)id_; }
    // Event payload carried in the LevelEventPacket data field.
    virtual int32_t networkData() const { return data_; }
    virtual ~Particle() = default;
private:
    ParticleId id_;
    Position pos_;
    int32_t data_;
};
} // namespace
