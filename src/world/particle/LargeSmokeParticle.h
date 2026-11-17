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
// LargeSmokeParticle — protocol-84 particle id 8 ("large_smoke").
// SPDX-License-Identifier: LGPL-3.0-or-later
// Family: None. The client ignores the payload; callers pass 0.
#pragma once
#include "GenericParticle.h"
#include "ParticleFamily.h"
namespace cursestone::world::particle {

class LargeSmokeParticle : public GenericParticle {
  public:
    static constexpr ParticleId kId = PARTICLE_LARGE_SMOKE;
    static constexpr DataKind kDataKind = DataKind::None;
    explicit LargeSmokeParticle(Position pos, int32_t data = 0)
        : GenericParticle(kId, pos, data) {
    }
};
} // namespace
