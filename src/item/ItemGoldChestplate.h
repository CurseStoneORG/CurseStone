#pragma once
#include "ItemArmor.h"
#include <string>
namespace cenisyspe::item {
class ItemGoldChestplate : public ItemArmor {
public:
    ItemGoldChestplate() : ItemArmor(315, ArmorMaterial::ARMOR_GOLD, ArmorSlot::CHESTPLATE, 4) {}
    std::string name() const override { return "Gold chestplate"; }
};
} // namespace
