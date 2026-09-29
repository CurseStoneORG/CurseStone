#pragma once
#include "ItemFood.h"
#include <string>
namespace cursestone::item {
class ItemSteak : public ItemFood {
public:
    ItemSteak() : ItemFood(364, 8) {}
    std::string name() const override { return "Steak"; }
};
} // namespace
