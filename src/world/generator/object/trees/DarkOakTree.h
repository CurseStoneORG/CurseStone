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
// SPDX-License-Identifier: LGPL-3.0-or-later
// DarkOakTree — vanilla dark oak ("roofed forest" tree): a thick 2x2
// trunk with a wide layered canopy. Ported from ext-vanillagenerator's
// DarkOakTree.
#pragma once
#include "GenericTree.h"

namespace cursestone::world::generator::object::trees {

class DarkOakTree : public GenericTree {
  public:
    bool generate(mcp_chunk_t* c, int sourceX, int sourceY, int sourceZ, uint8_t soilId,
                  MersenneRandom& rng) override;

  protected:
    void init(MersenneRandom& rng) override;
    bool canPlaceOn(uint8_t soil) const override;
    void canopy(mcp_chunk_t* c, int centerX, int trunkTopY, int centerZ, MersenneRandom& rng);
};

} // namespace cursestone::world::generator::object::trees
