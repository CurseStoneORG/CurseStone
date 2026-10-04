// SPDX-License-Identifier: LGPL-3.0-or-later
#include "TreeGenerator.h"
#include "trees/OakTree.h"
#include "trees/BigOakTree.h"
#include "trees/BirchTree.h"
#include "trees/SpruceTree.h"
#include "trees/JungleTree.h"
#include "trees/AcaciaTree.h"
#include "trees/DarkOakTree.h"
#include "trees/SwampTree.h"
#include "trees/BigJungleTree.h"

namespace cursestone::level::generator::object {

bool TreeGenerator::place(mcp_chunk_t* c, int x, int y, int z, TreeType t,
                          uint8_t soilId, GenRandom& rng) {
    switch (t) {
        case TREE_OAK: {
            trees::OakTree tr;     return tr.generate(c, x, y, z, soilId, rng);
        }
        case TREE_BIG_OAK: {
            trees::BigOakTree tr;  return tr.generate(c, x, y, z, soilId, rng);
        }
        case TREE_BIRCH: {
            trees::BirchTree tr;   return tr.generate(c, x, y, z, soilId, rng);
        }
        case TREE_SPRUCE: {
            trees::SpruceTree tr;  return tr.generate(c, x, y, z, soilId, rng);
        }
        case TREE_JUNGLE: {
            trees::JungleTree tr;  return tr.generate(c, x, y, z, soilId, rng);
        }
        case TREE_ACACIA: {
            trees::AcaciaTree tr;  return tr.generate(c, x, y, z, soilId, rng);
        }
        case TREE_DARK_OAK: {
            trees::DarkOakTree tr; return tr.generate(c, x, y, z, soilId, rng);
        }
        case TREE_SWAMP: {
            trees::SwampTree tr;   return tr.generate(c, x, y, z, soilId, rng);
        }
        case TREE_BIG_JUNGLE: {
            trees::BigJungleTree tr; return tr.generate(c, x, y, z, soilId, rng);
        }
        default: {
            trees::OakTree tr;     return tr.generate(c, x, y, z, soilId, rng);
        }
    }
}

} // namespace cursestone::level::generator::object
