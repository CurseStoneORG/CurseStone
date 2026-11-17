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
// WhiteSmokeParticle — protocol-84 particle id 5 ("white_smoke").
// SPDX-License-Identifier: LGPL-3.0-or-later
// Family: Scale. The client reads `data` as the scale/lifetime integer (0 = client default).
#pragma once
#include "GenericParticle.h"
#include "ParticleFamily.h"
namespace cursestone::world::particle {

class WhiteSmokeParticle : public GenericParticle {
  public:
    static constexpr ParticleId kId = PARTICLE_WHITE_SMOKE;
    static constexpr DataKind kDataKind = DataKind::Scale;
    explicit WhiteSmokeParticle(Position pos, int32_t data = 0)
        : GenericParticle(kId, pos, data) {
    }
};
} // namespace
