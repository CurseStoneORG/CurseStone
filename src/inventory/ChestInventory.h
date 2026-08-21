#pragma once
#include "Inventory.h"
namespace mcppeserver::inventory {
class ChestInventory : public Inventory {
public:
    ChestInventory() : Inventory(27) {}
};
} // namespace
