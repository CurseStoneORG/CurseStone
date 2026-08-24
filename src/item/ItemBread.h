#pragma once
#include "ItemFood.h"
#include <string>
namespace atomikpe::item {
class ItemBread : public ItemFood {
public:
    ItemBread() : ItemFood(297, 5) {}
    std::string name() const override { return "Bread"; }
};
} // namespace
