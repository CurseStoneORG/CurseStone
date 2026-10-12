// SPDX-License-Identifier: LGPL-3.0-or-later
// AcaciaTree — vanilla acacia: a twisting trunk with a flat umbrella
// canopy, sometimes a second branch. Ported from ext-vanillagenerator's
// AcaciaTree.
#pragma once
#include "GenericTree.h"

namespace cursestone::world::generator::object::trees {

class AcaciaTree : public GenericTree {
public:
    bool generate(mcp_chunk_t* c, int sourceX, int sourceY, int sourceZ,
                  uint8_t soilId, MersenneRandom& rng) override;

protected:
    void init(MersenneRandom& rng) override;
    bool canPlaceOn(uint8_t soil) const override;

private:
    // Places the flat canopy centred on (cx, trunkTopY, cz).
    void canopy(mcp_chunk_t* c, int cx, int trunkTopY, int cz,
                MersenneRandom& rng, int spread);
};

} // namespace cursestone::world::generator::object::trees
