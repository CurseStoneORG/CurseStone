#pragma once
#include "ItemArmor.h"
namespace mcppeserver::item {
class ItemLeatherLeggings : public ItemArmor {
public:
    ItemLeatherLeggings() : ItemArmor(300, ArmorMaterial::ARMOR_LEATHER, ArmorSlot::LEGGINGS, 2) {}
    std::string name() const override { return "Leather leggings"; }
};
} // namespace
