#pragma once
#include "ItemFood.h"
#include <string>
namespace atomikpe::item {
class ItemMelon : public ItemFood {
public:
    ItemMelon() : ItemFood(360, 2) {}
    std::string name() const override { return "Melon Slice"; }
};
} // namespace
