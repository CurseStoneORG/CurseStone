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
#include "block/BlockIds.h"
#include "item/ItemStack.h"
#include "item/ItemNbt.h"
#include "inventory/DoubleChestInventory.h"
#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/ListTag.h"
#include "utils/CRaii.h"
#include "world/World.h"
namespace cursestone::tile {

// ---- Vanilla double-chest pairing ----
// Same-Y is implied by the vanilla pairx/pairz layout: the partner is
// always looked up on the block position (pairx, y, pairz).
std::shared_ptr<TileChest> TileChest::pairTile(cursestone::world::World* lvl) const {
    if (!has_pair_ || !lvl)
        return nullptr;
    auto t = lvl->getTile(pair_x_, (int32_t)pos_.y, pair_z_);
    auto other = std::dynamic_pointer_cast<TileChest>(t);
    if (!other || other.get() == this)
        return nullptr;
    // The pairing only counts while the partner block actually exists —
    // guards against stale pair tags pointing at a position whose chest
    // block is already gone.
    if (lvl->getBlockId(pair_x_, (int32_t)pos_.y, pair_z_) != block::CHEST)
        return nullptr;
    return other;
}

void TileChest::setPair(const std::shared_ptr<TileChest>& other) {
    if (!other || other.get() == this)
        return;
    pair_x_ = (int32_t)other->position().x;
    pair_z_ = (int32_t)other->position().z;
    has_pair_ = true;
    double_.reset(); // rebuilt on the next open — the halves are set
    other->pair_x_ = (int32_t)position().x;
    other->pair_z_ = (int32_t)position().z;
    other->has_pair_ = true;
    other->double_.reset();
    markDirty();
    other->markDirty();
}

void TileChest::clearPair() {
    has_pair_ = false;
    pair_x_ = 0;
    pair_z_ = 0;
    double_.reset();
    markDirty();
}

std::shared_ptr<cursestone::inventory::DoubleChestInventory> TileChest::getOrCreateDouble(
    const std::shared_ptr<TileChest>& a, const std::shared_ptr<TileChest>& b) {
    if (!a || !b || a.get() == b.get())
        return nullptr;
    if (!a->isPaired() || !b->isPaired())
        return nullptr;
    if (a->double_ && a->double_ == b->double_)
        return a->double_;

    // Vanilla slot order (BlockChest.getLockableContainer): for a pair
    // laid out along the X axis the EAST half is addressed as slots 0-26;
    // for a pair along the Z axis the NORTH half is slots 0-26. Both
    // tiles cache the SAME wrapper instance, so co-viewers opening from
    // opposite halves share one slot space and one co-viewer sync.
    const auto& pa = a->position();
    const auto& pb = b->position();
    std::shared_ptr<TileChest> left, right;
    if ((int32_t)pb.x > (int32_t)pa.x) {
        left = b;
        right = a;
    } else if ((int32_t)pb.x < (int32_t)pa.x) {
        left = a;
        right = b;
    } else if ((int32_t)pb.z > (int32_t)pa.z) {
        left = a; // a is the north half
        right = b;
    } else {
        left = b;
        right = a;
    }

    a->double_ = std::make_shared<cursestone::inventory::DoubleChestInventory>(
        left->inventory(), right->inventory());
    b->double_ = a->double_;
    return a->double_;
}

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
    // Vanilla pairing persists as the two int tags every client reads:
    // without them a saved-and-reloaded double chest would render and
    // open as two single chests again.
    if (has_pair_) {
        mcp_compound_tag_set_int(tag, "pairx", pair_x_);
        mcp_compound_tag_set_int(tag, "pairz", pair_z_);
    }
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
    // Vanilla pairing tags (see writeNBT). INT_MIN as the sentinel — no
    // real chest block ever sits at that coordinate.
    constexpr int32_t kNoPair = (-2147483647 - 1);
    int32_t px = mcp_compound_tag_get_int(tag, "pairx", kNoPair);
    int32_t pz = mcp_compound_tag_get_int(tag, "pairz", kNoPair);
    if (px != kNoPair && pz != kNoPair) {
        pair_x_ = px;
        pair_z_ = pz;
        has_pair_ = true;
    }
    dirty_ = false; // just loaded from disk, nothing unsaved yet
}
} // namespace
