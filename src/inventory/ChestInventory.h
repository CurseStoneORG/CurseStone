#pragma once
#include "Inventory.h"
namespace atomikpe::inventory {
class ChestInventory : public Inventory {
public:
    ChestInventory() : Inventory(27) {}
};
} // namespace
