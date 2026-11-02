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
#include <algorithm>
#include <vector>
#include <memory>

namespace cursestone::block {
namespace {

// Flat light tables — the chunk light passes call lightOpacity/
// lightEmission hundreds of thousands of times per second (every cell
// of every sweep/flood), and each call used to be a registry load plus
// two virtual calls. A 256-entry lookup indexed by block id turns the
// hot path into one array read. registerBlock() refreshes the two
// slots for the registered id, so the tables always mirror the
// registry: unregistered ids keep the same fallback the registry path
// had (opacity 15 = solid, emission 0).
struct LightTables {
    uint8_t opacity[256];  // 15 = solid (unknown ids too)
    uint8_t emission[256]; // 0 = dark
    LightTables() {
        std::fill(std::begin(opacity), std::end(opacity), 15);
        std::fill(std::begin(emission), std::end(emission), 0);
    }
};
LightTables s_tables;

} // namespace

// Light query for the chunk light passes: how much light a block eats
// per step? Mirrors Minecraft's lightOpacity: 0 for air/leaves/glass/
// water etc (transparent), 15 (full block) for ordinary solids.
uint8_t Block::lightOpacity(uint8_t block_id) {
    if (block_id == 0)
        return 0; // air — fast path, same answer as the table
    return s_tables.opacity[block_id];
}

// Light query for the chunk light passes: how much light the block
// emits (0 for ordinary blocks, up to 15 for torch/glowstone/lava).
uint8_t Block::lightEmission(uint8_t block_id) {
    if (block_id == 0)
        return 0;
    return s_tables.emission[block_id];
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
    const bool transparent = b->isTransparent();
    const uint8_t level = b->getLightLevel();
    registry()[id] = std::move(b);
    // Keep the flat light tables in sync with the registry (they are
    // rebuilt on registration — see the LightTables note above).
    s_tables.opacity[id] = transparent ? 0 : 15;
    s_tables.emission[id] = level;
}
Block* Block::get(uint8_t id) {
    auto& r = registry();
    return r[id].get();
}
} // namespace
