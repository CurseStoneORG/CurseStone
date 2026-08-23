#pragma once
#include "ItemArmor.h"
#include <string>
namespace cenisyspe::item {
class ItemIronChestplate : public ItemArmor {
public:
    ItemIronChestplate() : ItemArmor(307, ArmorMaterial::ARMOR_IRON, ArmorSlot::CHESTPLATE, 6) {}
    std::string name() const override { return "Iron chestplate"; }
};
} // namespace
