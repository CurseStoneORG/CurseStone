#pragma once
#include "ItemFood.h"
namespace mcppeserver::item {
class ItemGoldenApple : public ItemFood {
public:
    ItemGoldenApple() : ItemFood(322, 8) {}
    std::string name() const override { return "Golden Apple"; }
};
} // namespace
