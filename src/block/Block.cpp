/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
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

// Generic flyweight standing in for ids no concrete Block was ever
// registered for. Block::get() must never hand out null: the join
// settle, the mob-egg ground scan and the bed-support probes all
// dereference the pointer bare, and a world holding one unregistered
// id used to segfault the server the moment a saved join spot sat
// inside it (the reported crash: tall grass 31, which the vanilla
// generator decorates plains and forests with, had no flyweight).
// Unknown ids behave like a plain full solid block — exactly what the
// guarded call sites already assumed for null (full collision box,
// full light opacity, not replaceable) — so registering the real ids
// only ever sharpens behavior, never flips it.
class BlockUnknown final : public Block {
  public:
    explicit BlockUnknown(uint8_t id) : id_(id) {}
    uint8_t id() const override {
        return id_;
    }
    std::string name() const override {
        return "Unknown (" + std::to_string(id_) + ")";
    }

  private:
    uint8_t id_;
};

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
// All 256 slots come pre-filled with the generic unknown flyweight at
// first touch (magic static — thread-safe init), so Block::get() stays
// a single branch-free array read that never returns null.
// register_all_blocks() overwrites the known ids on top; every id it
// misses keeps a usable instance instead of a crash.
std::vector<std::unique_ptr<Block>>& Block::registry() {
    static std::vector<std::unique_ptr<Block>> r = [] {
        std::vector<std::unique_ptr<Block>> v;
        v.reserve(256);
        for (uint32_t id = 0; id < 256; ++id)
            v.push_back(std::make_unique<BlockUnknown>(static_cast<uint8_t>(id)));
        return v;
    }();
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
