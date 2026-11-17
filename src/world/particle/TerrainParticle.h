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
// TerrainParticle — protocol-84 particle id 16 ("terrain").
// Family: Block. The client reads the payload as the block it shatters:
// block id in the low byte, meta in bits 8..11 (reference
// TerrainParticle). Air (id 0) has no break texture on the client, so
// validData() rejects it before it reaches the wire.
#pragma once
#include "GenericParticle.h"
#include "ParticleFamily.h"
namespace cursestone::world::particle {

class TerrainParticle : public GenericParticle {
  public:
    static constexpr ParticleId kId = PARTICLE_TERRAIN;
    static constexpr DataKind kDataKind = DataKind::Block;

    // Packs the payload the protocol-84 client parses: id low, meta at 8.
    static int32_t pack(uint8_t blockId, uint8_t meta) {
        return packBlockData(blockId, meta);
    }

    // False for payloads the 0.15.10 client cannot render: air carries no
    // break texture, so id 0 is rejected (meta is masked to 4 bits anyway).
    static bool validData(int32_t data) {
        return (((uint32_t)data) & 0xFFu) != 0u;
    }

    explicit TerrainParticle(Position pos, int32_t data = 0)
        : GenericParticle(kId, pos, data) {
    }
};
} // namespace
