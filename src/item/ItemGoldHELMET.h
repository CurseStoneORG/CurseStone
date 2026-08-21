#pragma once
#include "ItemArmor.h"
namespace mcppeserver::item {
class ItemGoldHELMET : public ItemArmor {
public:
    ItemGoldHELMET() : ItemArmor(314, ArmorMaterial::ARMOR_GOLD, ArmorSlot::HELMET, 2) {}
    std::string name() const override { return "Gold helmet"; }
};
} // namespace
