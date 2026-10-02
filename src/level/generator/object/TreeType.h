// SPDX-License-Identifier: LGPL-3.0-or-later
// TreeType — the tree species the generator can place.
// Shared by TreeGenerator, the per-tree-type implementations and the
// biome configs, so none of them need to include each other's headers.
#pragma once

namespace cursestone::level::generator::object {

enum TreeType {
    TREE_OAK = 0,
    TREE_BIG_OAK = 1,
    TREE_BIRCH = 2,
    TREE_SPRUCE = 3,
    TREE_JUNGLE = 4,
    TREE_ACACIA = 5,
    TREE_DARK_OAK = 6,
    TREE_SWAMP = 7,
};

} // namespace cursestone::level::generator::object
