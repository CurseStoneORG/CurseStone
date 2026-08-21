#pragma once
#include "ItemArmor.h"
namespace mcppeserver::item {
class ItemGoldLEGGINGS : public ItemArmor {
public:
    ItemGoldLEGGINGS() : ItemArmor(316, ArmorMaterial::ARMOR_GOLD, ArmorSlot::LEGGINGS, 3) {}
    std::string name() const override { return "Gold leggings"; }
};
} // namespace
