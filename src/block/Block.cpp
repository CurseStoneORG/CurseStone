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
#include "Block.h"
#include <vector>
#include <memory>

namespace cursestone::block {

// Light query for the chunk light passes: how much light a block eats
// per step? Mirrors Minecraft's lightOpacity: 0 for air/leaves/glass/
// water etc (transparent), 15 (full block) for ordinary solids.
uint8_t Block::lightOpacity(uint8_t block_id) {
    if (block_id == 0)
        return 0; // air — fast path, avoids touching the registry
    Block* b = Block::get(block_id);
    if (!b)
        return 15; // unknown id — treat as solid, same fallback as Block::get
    return b->isTransparent() ? 0 : 15;
}

// Light query for the chunk light passes: how much light the block
// emits (0 for ordinary blocks, up to 15 for torch/glowstone/lava).
uint8_t Block::lightEmission(uint8_t block_id) {
    if (block_id == 0)
        return 0;
    Block* b = Block::get(block_id);
    if (!b)
        return 0;
    return b->getLightLevel();
}

cursestone::math::AxisAlignedBB Block::getBoundingBox() const {
    return {0, 0, 0, 1, 1, 1};
}
std::vector<std::unique_ptr<Block>>& Block::registry() {
    static std::vector<std::unique_ptr<Block>> r;
    if (r.empty())
        r.resize(256);
    return r;
}
void Block::registerBlock(std::unique_ptr<Block> b) {
    uint8_t id = b->id();
    registry()[id] = std::move(b);
}
Block* Block::get(uint8_t id) {
    auto& r = registry();
    return r[id].get();
}
} // namespace
