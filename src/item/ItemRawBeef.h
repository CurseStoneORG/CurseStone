#pragma once
#include "ItemFood.h"
#include <string>
namespace mcppeserver::item {
class ItemRawBeef : public ItemFood {
public:
    ItemRawBeef() : ItemFood(363, 3) {}
    std::string name() const override { return "Raw Beef"; }
};
} // namespace
