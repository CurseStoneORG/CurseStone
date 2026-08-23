#pragma once
#include "ItemArmor.h"
#include <string>
namespace cenisyspe::item {
class ItemDiamondLeggings : public ItemArmor {
public:
    ItemDiamondLeggings() : ItemArmor(312, ArmorMaterial::ARMOR_DIAMOND, ArmorSlot::LEGGINGS, 6) {}
    std::string name() const override { return "Diamond leggings"; }
};
} // namespace
