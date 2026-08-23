#pragma once
#include "ItemFood.h"
#include <string>
namespace cenisyspe::item {
class ItemApple : public ItemFood {
public:
    ItemApple() : ItemFood(260, 4) {}
    std::string name() const override { return "Apple"; }
};
} // namespace
