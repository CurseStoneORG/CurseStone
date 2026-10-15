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
// SpruceTree (redwood) — vanilla conifer with a tapering layered canopy.
// Ported 1:1 from ext-vanillagenerator's RedwoodTree.
#pragma once
#include "GenericTree.h"

namespace cursestone::world::generator::object::trees {

class SpruceTree : public GenericTree {
  protected:
    void init(MersenneRandom& rng) override;
    bool canPlaceOn(uint8_t soil) const override; // spruce only on grass
    void placeCanopy(mcp_chunk_t* c, int sourceX, int sourceY, int sourceZ,
                     MersenneRandom& rng) override;
    void placeTrunk(mcp_chunk_t* c, int sourceX, int sourceY, int sourceZ,
                    MersenneRandom& rng) override;

  private:
    int leavesHeight_ = 1;
    int maxRadius_ = 2;
};

} // namespace cursestone::world::generator::object::trees
