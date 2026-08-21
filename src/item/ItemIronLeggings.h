#pragma once
#include "ItemArmor.h"
namespace mcppeserver::item {
class ItemIronLeggings : public ItemArmor {
public:
    ItemIronLEGGINGS() : ItemArmor(308, ArmorMaterial::ARMOR_IRON, ArmorSlot::LEGGINGS, 5) {}
    std::string name() const override { return "Iron leggings"; }
};
} // namespace
