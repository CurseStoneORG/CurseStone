#pragma once
#include "ItemArmor.h"
namespace mcppeserver::item {
class ItemIronBOOTS : public ItemArmor {
public:
    ItemIronBOOTS() : ItemArmor(309, ArmorMaterial::ARMOR_IRON, ArmorSlot::BOOTS, 2) {}
    std::string name() const override { return "Iron boots"; }
};
} // namespace
