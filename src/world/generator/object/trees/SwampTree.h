// SPDX-License-Identifier: LGPL-3.0-or-later
// SwampTree — vanilla swamp oak: wider drooping canopy, trunk that can
// stand in shallow water. Ported from ext-vanillagenerator's SwampTree.
#pragma once
#include "GenericTree.h"

namespace cursestone::world::generator::object::trees {

class SwampTree : public GenericTree {
protected:
    void init(GenRandom& rng) override;
    bool canPlaceOn(uint8_t soil) const override;
    void placeCanopy(mcp_chunk_t* c, int sourceX, int sourceY,
                     int sourceZ, GenRandom& rng) override;
    void placeTrunk(mcp_chunk_t* c, int sourceX, int sourceY,
                    int sourceZ, GenRandom& rng) override;
};

} // namespace cursestone::world::generator::object::trees
