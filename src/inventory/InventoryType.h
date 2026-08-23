// InventoryType — type IDs used by ContainerOpenPacket. Matches
// the MCPE 0.15.x network inventory types. Mirrors pocketmine's
// inventory/InventoryType.php enum.
#pragma once
#include <cstdint>
namespace cenisyspe::inventory {
enum InventoryType : uint8_t {
    INVTYPE_DEFAULT     = 0,
    INVTYPE_CHEST        = 0,
    INVTYPE_WORKBENCH    = 1,
    INVTYPE_FURNACE      = 2,
    INVTYPE_DISPENSER    = 3,
    INVTYPE_ENCHANT      = 4,
    INVTYPE_BREWING      = 5,
    INVTYPE_VILLAGER     = 6,
    INVTYPE_BEACON       = 7,
    INVTYPE_ANVIL        = 8,
    INVTYPE_HOPPER       = 9,
    INVTYPE_PLAYER       = 10,
    INVTYPE_UI           = 10,
    INVTYPE_SHULKERBOX   = 11,
    INVTYPE_ENDER_CHEST  = 11,
};
} // namespace
