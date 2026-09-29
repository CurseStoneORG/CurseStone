#pragma once
#include "ItemFood.h"
#include <string>
namespace cursestone::item {
class ItemRawChicken : public ItemFood {
public:
    ItemRawChicken() : ItemFood(366, 2) {}
    std::string name() const override { return "Raw Chicken"; }
};
} // namespace
