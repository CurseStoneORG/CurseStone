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
// ParticleFamily.cpp — dispatch of the per-family acceptance check to the
// particle files that own their payload packing.
#include "ParticleFamily.h"
#include "ItemBreakParticle.h"
#include "TerrainParticle.h"

namespace cursestone::world::particle {

bool isAcceptableParticleData(ParticleId id, int32_t data) {
    switch (id) {
    case PARTICLE_ITEM_BREAK:
        return ItemBreakParticle::validData(data);
    case PARTICLE_TERRAIN:
        return TerrainParticle::validData(data);
    default:
        return true;
    }
}

} // namespace cursestone::world::particle
