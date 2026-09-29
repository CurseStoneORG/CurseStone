// SPDX-License-Identifier: LGPL-3.0-or-later
// InventoryType.h — ported from cn.nukkit.inventory.InventoryType
//
// Registry of inventory "types" — each type defines the slot count, the
// human-readable title, and the network type id sent to the client in
// ContainerOpenPacket. Mirrors Nukkit's InventoryType.java exactly.
#pragma once
#include <string>
#include <unordered_map>
#include <memory>
namespace cursestone::inventory {

class InventoryType {
public:
    static constexpr int CHEST          = 0;
    static constexpr int DOUBLE_CHEST   = 1;
    static constexpr int PLAYER         = 2;
    static constexpr int FURNACE        = 3;
    static constexpr int CRAFTING       = 4;
    static constexpr int WORKBENCH      = 5;
    static constexpr int STONECUTTER   = 6;
    static constexpr int BREWING_STAND  = 7;
    static constexpr int ANVIL          = 8;
    static constexpr int ENCHANT_TABLE = 9;
    static constexpr int DISPENSER      = 10;
    static constexpr int DROPPER        = 11;
    static constexpr int HOPPER         = 12;

    int    defaultSize;
    std::string defaultTitle;
    int    typeId;

    InventoryType(int sz, const std::string& title, int tid)
        : defaultSize(sz), defaultTitle(title), typeId(tid) {}

    int getDefaultSize()  const { return defaultSize; }
    const std::string& getDefaultTitle() const { return defaultTitle; }
    int getNetworkType()  const { return typeId; }

    // Look up a registered type by index. Returns nullptr if unknown.
    static std::shared_ptr<InventoryType> get(int index);

    // Populate the registry with the built-in inventory types. Safe to
    // call multiple times — the second call is a no-op.
    static void init();

private:
    static std::unordered_map<int, std::shared_ptr<InventoryType>>& defaults();
    static bool initialized_;
};

} // namespace cursestone::inventory
