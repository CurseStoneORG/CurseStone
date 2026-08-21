#pragma once
#include "ItemArmor.h"
namespace mcppeserver::item {
class ItemGoldBOOTS : public ItemArmor {
public:
    ItemGoldBOOTS() : ItemArmor(317, ArmorMaterial::ARMOR_GOLD, ArmorSlot::BOOTS, 2) {}
    std::string name() const override { return "Gold boots"; }
};
} // namespace
