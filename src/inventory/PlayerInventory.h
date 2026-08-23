// PlayerInventory — 45 slots (36 main + 9 hotbar) + 4 armor slots.
// Mirrors pocketmine/inventory/PlayerInventory. The hotbar is the
// bottom row of the main grid; the "held slot" (0..8) selects the
// active item.
#pragma once
#include "Inventory.h"
#include <array>
namespace cenisyspe::inventory {
class PlayerInventory : public Inventory {
public:
    PlayerInventory() : Inventory(SIZE) {}
    static constexpr int SIZE = 45;
    static constexpr int ARMOR_OFFSET = 36;  // slots 36..39 are armor
    static constexpr int HELMET = 36, CHESTPLATE = 37, LEGGINGS = 38, BOOTS = 39;
    void setHeldSlot(int slot) { held_ = slot; }
    int heldSlot() const { return held_; }
    cenisyspe::item::ItemStack getHeldItem() const { return getItem(held_); }
    cenisyspe::item::ItemStack getHelmet()      const { return getItem(HELMET); }
    cenisyspe::item::ItemStack getChestplate() const { return getItem(CHESTPLATE); }
    cenisyspe::item::ItemStack getLeggings()    const { return getItem(LEGGINGS); }
    cenisyspe::item::ItemStack getBoots()       const { return getItem(BOOTS); }
private:
    int held_ = 0;
};
} // namespace
