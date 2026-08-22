#pragma once
#include "ItemArmor.h"
#include <string>
namespace mcppeserver::item {
class ItemIronHelmet : public ItemArmor {
public:
    ItemIronHelmet() : ItemArmor(306, ArmorMaterial::ARMOR_IRON, ArmorSlot::HELMET, 2) {}
    std::string name() const override { return "Iron helmet"; }
};
} // namespace
