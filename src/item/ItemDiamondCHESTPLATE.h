#pragma once
#include "ItemArmor.h"
namespace mcppeserver::item {
class ItemDiamondCHESTPLATE : public ItemArmor {
public:
    ItemDiamondCHESTPLATE() : ItemArmor(311, ArmorMaterial::ARMOR_DIAMOND, ArmorSlot::CHESTPLATE, 8) {}
    std::string name() const override { return "Diamond chestplate"; }
};
} // namespace
