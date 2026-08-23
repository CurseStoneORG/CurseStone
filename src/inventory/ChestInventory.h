#pragma once
#include "Inventory.h"
namespace cenisyspe::inventory {
class ChestInventory : public Inventory {
public:
    ChestInventory() : Inventory(27) {}
};
} // namespace
