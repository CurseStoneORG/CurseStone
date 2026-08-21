#pragma once
#include "ItemArmor.h"
namespace mcppeserver::item {
class ItemLeatherBOOTS : public ItemArmor {
public:
    ItemLeatherBOOTS() : ItemArmor(301, ArmorMaterial::ARMOR_LEATHER, ArmorSlot::BOOTS, 1) {}
    std::string name() const override { return "Leather boots"; }
};
} // namespace
