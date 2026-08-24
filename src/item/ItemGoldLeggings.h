#pragma once
#include "ItemArmor.h"
#include <string>
namespace atomikpe::item {
class ItemGoldLeggings : public ItemArmor {
public:
    ItemGoldLeggings() : ItemArmor(316, ArmorMaterial::ARMOR_GOLD, ArmorSlot::LEGGINGS, 3) {}
    std::string name() const override { return "Gold leggings"; }
};
} // namespace
