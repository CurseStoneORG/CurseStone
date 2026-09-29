#pragma once
#include "ItemArmor.h"
#include <string>
namespace cursestone::item {
class ItemDiamondBoots : public ItemArmor {
public:
    ItemDiamondBoots() : ItemArmor(313, ArmorMaterial::ARMOR_DIAMOND, ArmorSlot::BOOTS, 3) {}
    std::string name() const override { return "Diamond boots"; }
};
} // namespace
