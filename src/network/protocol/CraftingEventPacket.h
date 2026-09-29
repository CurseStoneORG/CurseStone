// SPDX-License-Identifier: LGPL-3.0-or-later
// CraftingEventPacket — client→server "el jugador crafteó esto" (MCPE
// protocol 84).
//
// PMMP/Genisys decode:
//   getByte()      windowId
//   getVarInt()    type
//   get(16)        id (UUID binario de la receta — el mismo que el
//                  server escribió en CraftingDataPacket)
//   getUnsignedVarInt() size; getSlot() × size    (input, máx 128)
//   getUnsignedVarInt() size; getSlot() × size    (output, máx 128)
//
// El server NUNCA encodea este packet (encode() de Genisys es vacío) —
// el flujo se cierra con ContainerSetContentPacket windowid=0.
#pragma once

#include "DataPacket.h"
#include "item/ItemStack.h"
#include <array>
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class CraftingEventPacket : public DataPacket {
public:
    uint8_t pid() const override { return CRAFTING_EVENT_PACKET; }

    static constexpr uint8_t CRAFTING_SMALL = 0;

    uint8_t windowId = 0;
    int32_t type = 0;
    // UUID binario de la receta — 16 bytes crudos en el mismo orden que
    // se escribieron en CraftingDataPacket. El match contra recetas va
    // por estos bytes.
    std::array<uint8_t, 16> id{};
    std::vector<cursestone::item::ItemStack> input;
    std::vector<cursestone::item::ItemStack> output;

    void encode(std::vector<uint8_t>& out) const override;   // no-op (PMMP)
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
