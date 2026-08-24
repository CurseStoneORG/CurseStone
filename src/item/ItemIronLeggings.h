#pragma once
#include "ItemArmor.h"
#include <string>
namespace atomikpe::item {
class ItemIronLeggings : public ItemArmor {
public:
    ItemIronLeggings() : ItemArmor(308, ArmorMaterial::ARMOR_IRON, ArmorSlot::LEGGINGS, 5) {}
    std::string name() const override { return "Iron leggings"; }
};
} // namespace
