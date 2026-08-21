#pragma once
#include "Inventory.h"
namespace mcppeserver::inventory {
class FurnaceInventory : public Inventory {
public:
    FurnaceInventory() : Inventory(3) {}
    int INPUT  = 0;
    int FUEL   = 1;
    int RESULT = 2;
};
} // namespace
