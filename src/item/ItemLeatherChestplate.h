#pragma once
#include "ItemArmor.h"
#include <string>
namespace mcppeserver::item {
class ItemLeatherChestplate : public ItemArmor {
public:
    ItemLeatherChestplate() : ItemArmor(299, ArmorMaterial::ARMOR_LEATHER, ArmorSlot::CHESTPLATE, 3) {}
    std::string name() const override { return "Leather tunic"; }
};
} // namespace
