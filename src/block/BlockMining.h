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
// BlockMining — vanilla mining rules per block id. Mirrors the Java
// hardness / tool-class / harvest-tier tables the 0.15 client mirrors:
//
//   breakSeconds = hardness * (canHarvest ? 1.5 : 5.0) / toolSpeed
//
// where toolSpeed applies only when the held tool's class matches the
// block's class (wood 2, gold 12, stone 4, iron 6, diamond 8) and the
// 5x penalty only bites tool-gated blocks (stone never yields to a hand,
// dirt does). `miningRule` is a jump-table switch — O(1) with no data
// segment; unlisted ids fall back to generic hand-harvestable stone.
#pragma once
#include <cstdint>

namespace cursestone::item {
struct ItemStack;
}

namespace cursestone::block {

// tool: item::ToolType the block mines faster with (PICKAXE=0, AXE=1,
// SHOVEL=2); T_NONE when no tool class applies. tier: -1 when the block
// is NOT tool-gated (drops by hand, the tool only speeds it up); >= 0 is
// the minimum harvest level the held tool must reach for a drop
// (wood/gold 0, stone 1, iron 2, diamond 3).
constexpr int8_t T_NONE = -1;

struct BlockMiningRule {
    float hardness; // 0 = instant break, < 0 = unbreakable (liquids).
    int8_t tool;
    int8_t tier;
};

// Whether the held stack may yield the block's drop (vanilla harvest).
bool canHarvest(uint8_t blockId, const item::ItemStack& held);

// Vanilla break time in seconds for the held stack: 0 = instant,
// -1 = unbreakable (the caller keeps its existing behavior for those).
float breakSeconds(uint8_t blockId, const item::ItemStack& held);

} // namespace cursestone::block
