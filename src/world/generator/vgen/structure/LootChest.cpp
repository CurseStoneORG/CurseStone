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
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "LootChest.h"

#include "block/BlockIds.h"
#include "diagnostics/ChestDebug.h"
#include "item/ItemNbt.h"
#include "nbt/NBT.h"
#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/ListTag.h"
#include "utils/CRaii.h"
#include "vgen/Blocks.h"
#include "world/loot/SimpleLoot.h"

namespace vgen {

bool placeLootChest(BlockSource& region, int x, int y, int z, DataID facing,
                    const std::string& lootTable, Random& random) {
    if (y < 0 || y >= 128)
        return false;

    // Roll the loot first: an unknown table still leaves the chest
    // standing, just empty (same outcome vanilla has for tables a server
    // does not ship).
    uint64_t seed = (uint64_t)(uint32_t)random.nextInt();
    seed ^= (uint64_t)(uint32_t)(x * 9781 + y * 631 + z * 3121) << 32;
    std::vector<cursestone::item::ItemStack> loot =
        cursestone::loot::rollTable(lootTable, seed);

    // Chest tile compound, little-endian — byte-for-byte the layout
    // World::syncTilesToChunks persists (id/x/y/z + Items list of
    // {Slot, id, Damage, Count} entries), so the first hydration reads a
    // structure chest exactly like a saved one.
    cursestone::utils::TagGuard root(new cursestone::nbt::CompoundTag());
    if (!root)
        return false;
    cursestone::nbt::set_string(root.get(), "id", "Chest");
    cursestone::nbt::set_int(root.get(), "x", (int32_t)x);
    cursestone::nbt::set_int(root.get(), "y", (int32_t)y);
    cursestone::nbt::set_int(root.get(), "z", (int32_t)z);
    // The facing lives in the BLOCK data on the wire, but the block cell
    // can be rebuilt without it (the tile-block heal replays a saved tile
    // whose block was stomped by a later generation write) — carry it in
    // the tile compound so a healed chest stands the way it was placed.
    cursestone::nbt::set_int(root.get(), "facing", (int32_t)facing);

    cursestone::utils::TagGuard items(new cursestone::nbt::ListTag("Items", cursestone::nbt::TAG_COMPOUND));
    if (!items)
        return false;

    // Vanilla loot placement: every rolled stack goes into a random slot,
    // probing forward when the slot is already taken.
    bool slots[27] = {};
    for (const auto& stack : loot) {
        int slot = (int)(random.nextInt() & 0x7fffffff) % 27;
        for (int probe = 0; probe < 27 && slots[slot]; ++probe)
            slot = (slot + 1) % 27;
        if (slots[slot])
            continue; // full chest — drop the remainder like vanilla rolls do
        slots[slot] = true;

        cursestone::utils::TagGuard entry(new cursestone::nbt::CompoundTag());
        if (!entry)
            continue;
        cursestone::nbt::set_byte(entry.get(), "Slot", (int8_t)slot);
        cursestone::item::itemStackToNbt(stack, entry.get());
        if (cursestone::nbt::append(items.get(), entry.get()))
            entry.release();
    }
    if (cursestone::nbt::set(root.get(), "Items", items.get()))
        items.release();

    std::vector<uint8_t> blob;
    cursestone::nbt::write(root.get(), /*little_endian=*/true, blob);
    if (blob.empty())
        return false;

    // Block first, then the tile blob — both follow the exact routing of
    // any structure write: inside the populated chunk or through the
    // neighbor sink. UPDATE_NONE matches structure placement (no
    // neighbor wake-ups during generation).
    const size_t blobLen = blob.size();
    if (!region.setBlockAndData(BlockPos(x, y, z), Block::mChest->mID, facing, Block::UPDATE_NONE))
        return false;
    region.addTileEntity(BlockPos(x, y, z), std::move(blob));
    cursestone::diagnostics::ChestDebug::onChestGenerated(x, y, z, (int)facing, lootTable.c_str(),
                                                          blobLen);
    return true;
}

} // namespace vgen
