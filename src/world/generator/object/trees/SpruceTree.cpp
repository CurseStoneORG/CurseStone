// SPDX-License-Identifier: LGPL-3.0-or-later
#include "SpruceTree.h"
#include "block/BlockIds.h"
#include <cstdlib>

namespace cursestone::world::generator::object::trees {

using cursestone::block::BlockId;

void SpruceTree::init(MersenneRandom& rng) {
    height_ = 6 + (int)rng.nextInt(4);        // 6..9 (reference RedwoodTree)
    leavesHeight_ = 1 + (int)rng.nextInt(2);  // 1..2
    maxRadius_ = 2 + (int)rng.nextInt(2);     // 2..3
    logId_ = 17;  logData_ = 1;   // LOG, spruce
    leafId_ = 18; leafData_ = 1;  // LEAVES, spruce
}

bool SpruceTree::canPlaceOn(uint8_t soil) const {
    return soil == (uint8_t)BlockId::GRASS;
}

// Reference RedwoodTree::Generate — canopy built from the top down, with a
// radius that widens step by step and whose minimum/peak grow as we go,
// producing the classic tapering conifer silhouette.
void SpruceTree::placeCanopy(mcp_chunk_t* c, int sourceX, int sourceY,
                             int sourceZ, MersenneRandom& rng) {
    int radius = (int)rng.nextInt(2);   // 0..1
    int peakRadius = 1;
    int minRadius = 0;

    for (int y = sourceY + height_; y >= sourceY + leavesHeight_; --y) {
        for (int x = sourceX - radius; x <= sourceX + radius; ++x) {
            for (int z = sourceZ - radius; z <= sourceZ + radius; ++z) {
                if (std::abs(x - sourceX) != radius || std::abs(z - sourceZ) != radius ||
                    radius <= 0) {
                    setLeaf(c, x, y, z, leafId_, leafData_);
                }
            }
        }
        if (radius >= peakRadius) {
            radius = minRadius;
            minRadius = 1;
            if (++peakRadius > maxRadius_) peakRadius = maxRadius_;
        } else {
            radius++;
        }
    }
}

void SpruceTree::placeTrunk(mcp_chunk_t* c, int sourceX, int sourceY,
                            int sourceZ, MersenneRandom& rng) {
    int h = height_ - (int)rng.nextInt(3);
    for (int i = 0; i < h; ++i)
        setLog(c, sourceX, sourceY + i, sourceZ, logId_, logData_);
}

} // namespace cursestone::world::generator::object::trees
