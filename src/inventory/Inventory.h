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
    // On-change callback (used by player + tile entities).
    using SlotChangeFn = std::function<void(int slot, const atomikpe::item::ItemStack&)>;
    void setSlotChangeCallback(SlotChangeFn cb) { on_slot_change_ = std::move(cb); }
protected:
    std::vector<atomikpe::item::ItemStack> items_;
    SlotChangeFn on_slot_change_;
};
} // namespace
