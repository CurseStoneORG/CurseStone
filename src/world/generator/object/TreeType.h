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
// TreeType — the tree species the generator can place.
// Shared by TreeGenerator, the per-tree-type implementations and the
// biome configs, so none of them need to include each other's headers.
#pragma once

namespace cursestone::world::generator::object {

enum TreeType {
    TREE_OAK = 0,
    TREE_BIG_OAK = 1,
    TREE_BIRCH = 2,
    TREE_SPRUCE = 3,
    TREE_JUNGLE = 4,
    TREE_ACACIA = 5,
    TREE_DARK_OAK = 6,
    TREE_SWAMP = 7,
    // Vanilla big jungle tree: tall buttressed trunk, branch arms with leaf
    // clusters, layered crown and vines. Ported from Aquamarine/PocketMine.
    TREE_BIG_JUNGLE = 8,
};

} // namespace cursestone::world::generator::object
