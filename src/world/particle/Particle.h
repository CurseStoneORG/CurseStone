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
// Particle — abstract level particle. Particles are sent to players
// in range via LevelEventPacket (evid = EVENT_ADD_PARTICLE_MASK | type).
// The server-side particle object only computes position and forwards it
// to the network layer.
//
// One concrete particle per file lives beside this header: each type owns
// its id (ParticleId.h), its payload family (ParticleFamily.h) and, for
// the structured families, its own packing + validation.
#pragma once
#include "ParticleId.h"
#include "world/Position.h"
#include <cstdint>
namespace cursestone::world::particle {

class Particle {
  public:
    // data = event payload (particle scale / color), protocol 84.
    Particle(ParticleId id, Position pos, int32_t data = 0) : id_(id), pos_(pos), data_(data) {
    }
    ParticleId id() const {
        return id_;
    }
    const Position& position() const {
        return pos_;
    }
    // Wire type id (low 12 bits of the LevelEventPacket event field).
    virtual int32_t networkId() const {
        return (int32_t)id_;
    }
    // Event payload carried in the LevelEventPacket data field.
    virtual int32_t networkData() const {
        return data_;
    }
    virtual ~Particle() = default;

  private:
    ParticleId id_;
    Position pos_;
    int32_t data_;
};
} // namespace
