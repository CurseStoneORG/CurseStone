#pragma once
#include "ItemArmor.h"
namespace mcppeserver::item {
class ItemIronChestplate : public ItemArmor {
public:
    ItemIronCHESTPLATE() : ItemArmor(307, ArmorMaterial::ARMOR_IRON, ArmorSlot::CHESTPLATE, 6) {}
    std::string name() const override { return "Iron chestplate"; }
};
} // namespace
