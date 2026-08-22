#pragma once
#include "ItemArmor.h"
namespace mcppeserver::item {
class ItemIronBoots : public ItemArmor {
public:
    ItemIronBoots() : ItemArmor(309, ArmorMaterial::ARMOR_IRON, ArmorSlot::BOOTS, 2) {}
    std::string name() const override { return "Iron boots"; }
};
} // namespace
