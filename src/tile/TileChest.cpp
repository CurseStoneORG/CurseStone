#include "TileChest.h"
#include "item/ItemStack.h"
#include "nbt/tag/CompoundTag.h"
#include "nbt/tag/ListTag.h"
namespace cursestone::tile {
void TileChest::setItem(int slot, const cursestone::item::ItemStack& it) {
    if (slot < 0 || slot >= SIZE) return;
    inventory_->setItem(slot, it);
    dirty_ = true;
}
cursestone::item::ItemStack TileChest::getItem(int slot) const {
    if (slot < 0 || slot >= SIZE) return cursestone::item::ItemStack::EMPTY();
    return inventory_->getItem(slot);
}
void TileChest::removeItem(int slot) {
    if (slot < 0 || slot >= SIZE) return;
    inventory_->clear(slot);
    dirty_ = true;
}

// Items serialize as PMMP/Nukkit legacy do: a "Items" ListTag of
// per-slot compounds {Slot (byte), id (short), Damage (short),
// Count (byte)}. Empty slots are skipped entirely (not written as
// zeroed entries) — that's what PMMP legacy does, and it keeps the
// list from growing to 27 entries for a chest that only has 2 items
// in it. On read, any slot not present in the list is left empty,
// which is already Container::clear()'s default state.
void TileChest::writeNBT(mcp_tag_t* tag) const {
    if (!tag) return;
    Tile::writeNBT(tag);  // writes id/x/y/z
    mcp_tag_t* items = mcp_list_tag_new_named("Items", MCP_TAG_COMPOUND);
    auto contents = inventory_->getContents();
    for (const auto& [slot, stack] : contents) {
        if (stack.isEmpty()) continue;
        if (slot < 0 || slot >= SIZE) continue;
        mcp_tag_t* entry = mcp_compound_tag_new();
        mcp_compound_tag_set_byte(entry, "Slot", static_cast<int8_t>(slot));
        mcp_compound_tag_set_short(entry, "id", static_cast<int16_t>(stack.id));
        mcp_compound_tag_set_short(entry, "Damage", static_cast<int16_t>(stack.meta));
        mcp_compound_tag_set_byte(entry, "Count", static_cast<int8_t>(stack.count));
        mcp_list_tag_append(items, entry);
    }
    mcp_compound_tag_set(tag, "Items", items);
}

void TileChest::readNBT(const mcp_tag_t* tag) {
    if (!tag) return;
    Tile::readNBT(tag);
    const mcp_tag_t* items = mcp_compound_tag_get(tag, "Items");
    if (!items || mcp_tag_get_type(items) != MCP_TAG_LIST) return;
    size_t count = mcp_list_tag_size(items);
    for (size_t i = 0; i < count; ++i) {
        const mcp_tag_t* entry = mcp_list_tag_get(items, i);
        if (!entry || mcp_tag_get_type(entry) != MCP_TAG_COMPOUND) continue;
        int slot = mcp_compound_tag_get_byte(entry, "Slot", -1);
        if (slot < 0 || slot >= SIZE) continue;
        cursestone::item::ItemStack stack;
        stack.id = static_cast<uint16_t>(mcp_compound_tag_get_short(entry, "id", 0));
        stack.meta = static_cast<uint8_t>(mcp_compound_tag_get_short(entry, "Damage", 0));
        stack.count = mcp_compound_tag_get_byte(entry, "Count", 0);
        if (stack.isEmpty()) continue;
        inventory_->setItem(slot, stack);
    }
    dirty_ = false;  // just loaded from disk, nothing unsaved yet
}
} // namespace
