// SPDX-License-Identifier: LGPL-3.0-or-later
// ContainerSetContentPacket — server→client full inventory contents.
//
// PMMP wire format (protocol 84) — ContainerSetContentPacket::encode:
//   [1 byte ID 0x2e]
//   [Uint8]    windowid   (0=main, 0x78=armor, 0x79=creative, 0x7a=hotbar)
//   [Int16 BE] count      number of slots
//   [Slot[count]] slots   each slot = Int16 BE id, if id!=0: count + damage
//   [Int16 BE] hotbarCount (always 0 unless windowid==0)
//   [Int32 BE hotbar[count]] hotbar link indices (only when windowid==0)
//
// PMMP source: pocketmine/network/protocol/ContainerSetContentPacket.php
#pragma once

#include "DataPacket.h"
#include "item/ItemStack.h"
#include <vector>
#include <cstdint>

namespace cenisyspe::network::protocol {

class ContainerSetContentPacket : public DataPacket {
public:
    uint8_t pid() const override { return CONTAINER_SET_CONTENT_PACKET; }

    // Window IDs (PMMP ContainerSetContentPacket constants).
    static constexpr uint8_t SPECIAL_INVENTORY = 0x00;
    static constexpr uint8_t SPECIAL_ARMOR     = 0x78;
    static constexpr uint8_t SPECIAL_CREATIVE  = 0x79;
    static constexpr uint8_t SPECIAL_HOTBAR    = 0x7a;

    uint8_t     windowid = 0;
    std::vector<cenisyspe::item::ItemStack> slots;
    std::vector<int32_t> hotbar;   // only used when windowid == SPECIAL_INVENTORY

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cenisyspe::network::protocol
