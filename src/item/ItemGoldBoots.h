#pragma once
#include "ItemArmor.h"
#include <string>
namespace mcppeserver::item {
class ItemGoldBoots : public ItemArmor {
public:
    ItemGoldBoots() : ItemArmor(317, ArmorMaterial::ARMOR_GOLD, ArmorSlot::BOOTS, 2) {}
    std::string name() const override { return "Gold boots"; }
};
} // namespace
