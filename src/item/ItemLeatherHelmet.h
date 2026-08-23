#pragma once
#include "ItemArmor.h"
#include <string>
namespace cenisyspe::item {
class ItemLeatherHelmet : public ItemArmor {
public:
    ItemLeatherHelmet() : ItemArmor(298, ArmorMaterial::ARMOR_LEATHER, ArmorSlot::HELMET, 1) {}
    std::string name() const override { return "Leather cap"; }
};
} // namespace
