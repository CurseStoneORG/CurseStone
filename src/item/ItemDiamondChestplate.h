#pragma once
#include "ItemArmor.h"
#include <string>
namespace cenisyspe::item {
class ItemDiamondChestplate : public ItemArmor {
public:
    ItemDiamondChestplate() : ItemArmor(311, ArmorMaterial::ARMOR_DIAMOND, ArmorSlot::CHESTPLATE, 8) {}
    std::string name() const override { return "Diamond chestplate"; }
};
} // namespace
