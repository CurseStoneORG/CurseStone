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
// BigJungleTree — the vanilla "big jungle tree": a very tall buttressed
// trunk, horizontal branches each capped with its own leaf cluster, a
// layered crown, and vines hanging off the wood.
//
// Ported 1:1 from Aquamarine-Team/Aquamarine
// (src/world/generator/normal/object/BigJungleTree, by MaruselPlay),
// which is itself the reference server HugeTree algorithm. The original hierarchy is
// flattened into this one class because CurseStone's tree base
// (GenericTree) is not a HugeTree:
//
//   * getHeight()             -> init() sets height_ from kBaseHeight +
//                                rng[0, kExtraRandom-1]
//   * ensureGrowable()        -> the room/soil checks GenericTree::generate
//                                already does, done up front here
//   * canOverride()           -> canOverrideAt()
//   * growLeavesLayer()       -> growLeavesLayer()
//   * growLeavesLayerStrict() -> growLeavesLayerStrict()
//   * setBlockAndNotifyAdequately() -> GenericTree::setForced() (writes
//                                unconditionally, clipped to the chunk)
//   * placeVine()             -> placeVine()
//
// Two deliberate, documented deviations from the original:
//   1. The reference passes plain Block::LOG / Block::LEAVES with no
//      metadata. CurseStone's protocol stores the wood/leaf VARIANT in the
//      block data byte, so this uses data 3 (jungle) — the same pair
//      JungleTree uses — otherwise the client would draw an oak tree.
//   2. The reference's first pair of assignments inside the branch loop
//      (0.5 + cos*f*4.0) is dead: the inner i1 loop overwrites k/l before
//      anything reads them. It is dropped here; the value it computed is
//      unchanged.
#pragma once
#include "GenericTree.h"

namespace cursestone::world::generator::object::trees {

class BigJungleTree : public GenericTree {
  public:
    bool generate(mcp_chunk_t* c, int sourceX, int sourceY, int sourceZ, uint8_t soilId,
                  MersenneRandom& rng) override;

  protected:
    void init(MersenneRandom& rng) override;

  private:
    // Leaf disc of `radius` with the corners randomly culled.
    void growLeavesLayer(mcp_chunk_t* c, int x, int y, int z, int radius, MersenneRandom& rng);
    // Solid leaf square of `radius`, no culling.
    void growLeavesLayerStrict(mcp_chunk_t* c, int x, int y, int z, int radius);
    // A hanging vine on the given face (meta is the face bitmask).
    void placeVine(mcp_chunk_t* c, MersenneRandom& rng, int x, int y, int z, uint8_t faceMask);
    // The reference's canOverride(): can this block be replaced by wood?
    static bool canOverrideAt(mcp_chunk_t* c, int x, int y, int z);
};

} // namespace cursestone::world::generator::object::trees