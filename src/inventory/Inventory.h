// Inventory — abstract inventory of N slots. Each slot holds an
// ItemStack. Subclasses: PlayerInventory (45 slots), ChestInventory
// (27 slots), FurnaceInventory (3 slots), etc.
#pragma once
#include "item/ItemStack.h"
#include <vector>
#include <functional>
namespace atomikpe::inventory {
class Inventory {
public:
    Inventory(int size) : items_(size) {}
    virtual ~Inventory() = default;
    virtual int size() const { return (int)items_.size(); }
    virtual int maxSize() const { return 64; }
    // Slot access. Out-of-range access returns empty ItemStack.
    virtual atomikpe::item::ItemStack getItem(int slot) const;
    virtual void setItem(int slot, const atomikpe::item::ItemStack& it);
    virtual void clear(int slot = -1);

    // Returns true if at least `item.count` units can fit somewhere
    // in this inventory (stacking into existing same-id/same-meta slots
    // or filling empty slots). Mirrors Nukkit BaseInventory.canAddItem().
    // Always returns true when `item.isEmpty()` (nothing to add).
    virtual bool canAddItem(const atomikpe::item::ItemStack& item) const;

    // Tries to add `item` into this inventory. Stacks with same id+meta
    // first, then fills empty slots. Returns the LEFTOVER (the part that
    // didn't fit) — if leftover.count <= 0 the whole stack was added.
    // Mirrors Nukkit BaseInventory.addItem(Item... slots).
    virtual atomikpe::item::ItemStack addItem(atomikpe::item::ItemStack item);

    // On-change callback (used by player + tile entities).
    using SlotChangeFn = std::function<void(int slot, const atomikpe::item::ItemStack&)>;
    void setSlotChangeCallback(SlotChangeFn cb) { on_slot_change_ = std::move(cb); }
protected:
    std::vector<atomikpe::item::ItemStack> items_;
    SlotChangeFn on_slot_change_;
};
} // namespace
