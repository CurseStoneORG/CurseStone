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
#include "TileChest.h"
#include "item/ItemStack.h"
#include "item/ItemNbt.h"
#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/ListTag.h"
#include "utils/CRaii.h"
namespace cursestone::tile {
void TileChest::setItem(int slot, const cursestone::item::ItemStack& it) {
    if (slot < 0 || slot >= SIZE)
        return;
    inventory_->setItem(slot, it);
    dirty_ = true;
}
cursestone::item::ItemStack TileChest::getItem(int slot) const {
    if (slot < 0 || slot >= SIZE)
        return cursestone::item::ItemStack::EMPTY();
    return inventory_->getItem(slot);
}
void TileChest::removeItem(int slot) {
    if (slot < 0 || slot >= SIZE)
        return;
    inventory_->clear(slot);
    dirty_ = true;
}

// Items serialize as the reference server/Nukkit legacy do: an "Items" ListTag of
// per-slot compounds {Slot (byte), id (short), Damage (short),
// Count (byte), ench (list)} — the item fields themselves come from
// item::itemStackToNbt()/itemStackFromNbt(), see src/item/ItemNbt.h.
// Empty slots are skipped entirely (not written as
// zeroed entries) — that's what the reference server legacy does, and it keeps the
// list from growing to 27 entries for a chest that only has 2 items
// in it. On read, any slot not present in the list is left empty,
// which is already Container::clear()'s default state.
void TileChest::writeNBT(mcp_tag_t* tag) const {
    if (!tag)
        return;
    Tile::writeNBT(tag); // writes id/x/y/z
    utils::TagGuard items(mcp_list_tag_new_named("Items", MCP_TAG_COMPOUND));
    if (!items)
        return;
    auto contents = inventory_->getContents();
    for (const auto& [slot, stack] : contents) {
        if (stack.isEmpty())
            continue;
        if (slot < 0 || slot >= SIZE)
            continue;
        utils::TagGuard entry(mcp_compound_tag_new());
        if (!entry)
            continue;
        mcp_compound_tag_set_byte(entry.get(), "Slot", static_cast<int8_t>(slot));
        item::itemStackToNbt(stack, entry.get());
        if (mcp_list_tag_append(items.get(), entry.get()) == 0)
            entry.release();
    }
    if (mcp_compound_tag_set(tag, "Items", items.get()) == 0)
        items.release();
}

void TileChest::readNBT(const mcp_tag_t* tag) {
    if (!tag)
        return;
    Tile::readNBT(tag);
    const mcp_tag_t* items = mcp_compound_tag_get(tag, "Items");
    if (!items || mcp_tag_get_type(items) != MCP_TAG_LIST)
        return;
    size_t count = mcp_list_tag_size(items);
    for (size_t i = 0; i < count; ++i) {
        const mcp_tag_t* entry = mcp_list_tag_get(items, i);
        if (!entry || mcp_tag_get_type(entry) != MCP_TAG_COMPOUND)
            continue;
        int slot = mcp_compound_tag_get_byte(entry, "Slot", -1);
        if (slot < 0 || slot >= SIZE)
            continue;
        // id / Count / Damage + enchantments, shared with every other
        // container (src/item/ItemNbt.h).
        item::ItemStack stack;
        if (!item::itemStackFromNbt(entry, stack))
            continue;
        inventory_->setItem(slot, stack);
    }
    dirty_ = false; // just loaded from disk, nothing unsaved yet
}
} // namespace
