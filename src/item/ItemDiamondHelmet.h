#pragma once
#include "ItemArmor.h"
#include <string>
namespace mcppeserver::item {
class ItemDiamondHelmet : public ItemArmor {
public:
    ItemDiamondHelmet() : ItemArmor(310, ArmorMaterial::ARMOR_DIAMOND, ArmorSlot::HELMET, 3) {}
    std::string name() const override { return "Diamond helmet"; }
};
} // namespace
