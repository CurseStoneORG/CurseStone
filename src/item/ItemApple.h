#pragma once
#include "ItemFood.h"
#include <string>
namespace atomikpe::item {
class ItemApple : public ItemFood {
public:
    ItemApple() : ItemFood(260, 4) {}
    std::string name() const override { return "Apple"; }
};
} // namespace
