#pragma once
#include "ItemArmor.h"
namespace mcppeserver::item {
class ItemIronHELMET : public ItemArmor {
public:
    ItemIronHELMET() : ItemArmor(306, ArmorMaterial::ARMOR_IRON, ArmorSlot::HELMET, 2) {}
    std::string name() const override { return "Iron helmet"; }
};
} // namespace
