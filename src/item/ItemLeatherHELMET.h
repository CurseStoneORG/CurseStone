#pragma once
#include "ItemArmor.h"
namespace mcppeserver::item {
class ItemLeatherHELMET : public ItemArmor {
public:
    ItemLeatherHELMET() : ItemArmor(298, ArmorMaterial::ARMOR_LEATHER, ArmorSlot::HELMET, 1) {}
    std::string name() const override { return "Leather helmet"; }
};
} // namespace
