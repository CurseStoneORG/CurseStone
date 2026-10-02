// SPDX-License-Identifier: LGPL-3.0-or-later
// DarkOakTree — vanilla dark oak ("roofed forest" tree): a thick 2x2
// trunk with a wide layered canopy. Ported from ext-vanillagenerator's
// DarkOakTree.
#pragma once
#include "GenericTree.h"

namespace cursestone::level::generator::object::trees {

class DarkOakTree : public GenericTree {
public:
    bool generate(mcp_chunk_t* c, int sourceX, int sourceY, int sourceZ,
                  GenRandom& rng) override;

protected:
    void init(GenRandom& rng) override;
    bool canPlaceOn(uint8_t soil) const override;
    void canopy(mcp_chunk_t* c, int centerX, int trunkTopY, int centerZ,
                GenRandom& rng);
};

} // namespace cursestone::level::generator::object::trees
