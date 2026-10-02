// SPDX-License-Identifier: LGPL-3.0-or-later
// BigOakTree — vanilla "fancy" oak: a tall trunk with branches, each
// ending in its own leaf cluster. Ported (adapted to our single-chunk
// writer) from ext-vanillagenerator's BigOakTree.
#pragma once
#include "GenericTree.h"

namespace cursestone::level::generator::object::trees {

class BigOakTree : public GenericTree {
protected:
    void init(GenRandom& rng) override;
    void placeCanopy(mcp_chunk_t* c, int sourceX, int sourceY,
                     int sourceZ, GenRandom& rng) override;
    void placeTrunk(mcp_chunk_t* c, int sourceX, int sourceY,
                    int sourceZ, GenRandom& rng) override;

private:
    int trunkHeight_ = 5;
};

} // namespace cursestone::level::generator::object::trees
