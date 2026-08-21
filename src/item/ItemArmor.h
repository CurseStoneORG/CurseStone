#pragma once
#include "Item.h"
#include <cstdint>
namespace mcppeserver::item {
enum ArmorMaterial : int {
    ARMOR_LEATHER = 0, ARMOR_CHAIN = 1, ARMOR_IRON = 2, ARMOR_GOLD = 3, ARMOR_DIAMOND = 4,
};
enum ArmorSlot : int {
    HELMET = 0, CHESTPLATE = 1, LEGGINGS = 2, BOOTS = 3,
};
class ItemArmor : public Item {
public:
    ItemArmor(uint16_t id, ArmorMaterial m, ArmorSlot s, int defense)
        : Item(id), mat_(m), slot_(s), defense_(defense) {}
    bool isArmor() const override { return true; }
    int maxStackSize() const override { return 1; }
    ArmorMaterial material() const { return mat_; }
    ArmorSlot slot() const { return slot_; }
    int defensePoints() const { return defense_; }
private:
    ArmorMaterial mat_;
    ArmorSlot slot_;
    int defense_;
};
} // namespace
