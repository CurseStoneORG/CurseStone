#pragma once
#include "ItemArmor.h"
namespace mcppeserver::item {
class ItemGoldChestplate : public ItemArmor {
public:
    ItemGoldCHESTPLATE() : ItemArmor(315, ArmorMaterial::ARMOR_GOLD, ArmorSlot::CHESTPLATE, 4) {}
    std::string name() const override { return "Gold chestplate"; }
};
} // namespace
