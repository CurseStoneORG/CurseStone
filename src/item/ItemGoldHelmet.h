#pragma once
#include "ItemArmor.h"
#include <string>
namespace cenisyspe::item {
class ItemGoldHelmet : public ItemArmor {
public:
    ItemGoldHelmet() : ItemArmor(314, ArmorMaterial::ARMOR_GOLD, ArmorSlot::HELMET, 2) {}
    std::string name() const override { return "Gold helmet"; }
};
} // namespace
