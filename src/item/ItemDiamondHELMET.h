#pragma once
#include "ItemArmor.h"
namespace mcppeserver::item {
class ItemDiamondHELMET : public ItemArmor {
public:
    ItemDiamondHELMET() : ItemArmor(310, ArmorMaterial::ARMOR_DIAMOND, ArmorSlot::HELMET, 3) {}
    std::string name() const override { return "Diamond helmet"; }
};
} // namespace
