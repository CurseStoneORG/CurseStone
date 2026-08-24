// SPDX-License-Identifier: LGPL-3.0-or-later
// ContainerSetContentPacket — server→client full inventory contents.
//
// PMMP wire format (protocol 84) — ContainerSetContentPacket::encode:
//   [1 byte ID 0x2e]
//   [Uint8]    windowid   (0=main, 0x78=armor, 0x79=creative, 0x7a=hotbar)
//   [Int16 BE] count      number of slots
//   [Slot[count]] slots   each slot = id BE + count + damage BE + nbtLen LE + nbt
//   [Int16 BE] hotbarCount (always 0 unless windowid==0)
//   [Int32 BE hotbar[count]] hotbar link indices (only when windowid==0)
//
// PMMP Slot (BinaryStream::putSlot):
//   if (id == 0)  write ONLY putShort(0) — 2 bytes total.
//   else:
//     putShort(id)         // 2 bytes BE
//     putByte(count)       // 1 byte
//     putShort(damage)     // 2 bytes BE
//     putLShort(nbtLen)    // 2 bytes LE  ← mixed endianness!
//     put(nbt)             // nbtLen bytes
//
// Empty slot has NO further bytes after the 0 id. This is critical:
// writing 5+ bytes for an empty slot shifts every subsequent slot's id
// by 3+ bytes, producing "duplicate Stone Shovels" / wrong items.
#pragma once

#include "DataPacket.h"
#include "item/ItemStack.h"
#include <vector>
#include <cstdint>

namespace atomikpe::network::protocol {

class ContainerSetContentPacket : public DataPacket {
public:
    uint8_t pid() const override { return CONTAINER_SET_CONTENT_PACKET; }

    // Window IDs (PMMP ContainerSetContentPacket constants).
    static constexpr uint8_t SPECIAL_INVENTORY = 0x00;
    static constexpr uint8_t SPECIAL_ARMOR     = 0x78;
    static constexpr uint8_t SPECIAL_CREATIVE  = 0x79;
    static constexpr uint8_t SPECIAL_HOTBAR    = 0x7a;

    uint8_t     windowid = 0;
    std::vector<atomikpe::item::ItemStack> slots;
    std::vector<int32_t> hotbar;   // only used when windowid == SPECIAL_INVENTORY

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace atomikpe::network::protocol
