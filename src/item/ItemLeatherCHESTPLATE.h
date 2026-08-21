#pragma once
#include "ItemArmor.h"
namespace mcppeserver::item {
class ItemLeatherCHESTPLATE : public ItemArmor {
public:
    ItemLeatherCHESTPLATE() : ItemArmor(299, ArmorMaterial::ARMOR_LEATHER, ArmorSlot::CHESTPLATE, 3) {}
    std::string name() const override { return "Leather chestplate"; }
};
} // namespace
