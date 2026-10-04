// SPDX-License-Identifier: LGPL-3.0-or-later
// SpruceTree (redwood) — vanilla conifer with a tapering layered canopy.
// Ported 1:1 from ext-vanillagenerator's RedwoodTree.
#pragma once
#include "GenericTree.h"

namespace cursestone::world::generator::object::trees {

class SpruceTree : public GenericTree {
protected:
    void init(GenRandom& rng) override;
    bool canPlaceOn(uint8_t soil) const override;   // spruce only on grass
    void placeCanopy(mcp_chunk_t* c, int sourceX, int sourceY,
                     int sourceZ, GenRandom& rng) override;
    void placeTrunk(mcp_chunk_t* c, int sourceX, int sourceY,
                    int sourceZ, GenRandom& rng) override;

private:
    int leavesHeight_ = 1;
    int maxRadius_ = 2;
};

} // namespace cursestone::world::generator::object::trees
