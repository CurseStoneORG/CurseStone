#pragma once
#include "ItemFood.h"
#include <string>
namespace cenisyspe::item {
class ItemRawChicken : public ItemFood {
public:
    ItemRawChicken() : ItemFood(366, 2) {}
    std::string name() const override { return "Raw Chicken"; }
};
} // namespace
