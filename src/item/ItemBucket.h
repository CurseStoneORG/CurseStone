#pragma once
#include "Item.h"
#include <string>
namespace mcppeserver::item {
class ItemBucket : public Item {
public:
    ItemBucket() : Item(325) {}
    std::string name() const override { return "Bucket"; }
};
} // namespace
