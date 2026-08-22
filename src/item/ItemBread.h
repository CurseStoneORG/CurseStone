#pragma once
#include "ItemFood.h"
namespace mcppeserver::item {
class ItemBread : public ItemFood {
public:
    ItemBread() : ItemFood(297, 5) {}
    std::string name() const override { return "Bread"; }
};
} // namespace
