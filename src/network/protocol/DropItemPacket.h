// SPDX-License-Identifier: LGPL-3.0-or-later
// DropItemPacket — cliente→servidor. El jugador suelta el item del slot
// seleccionado (Q, o arrastrar fuera del inventario con ventana abierta).
//
// Wire format (protocolo 84, mirrors Genisys DropItemPacket.php):
//   [Byte] type                     // 0 = drop, 1 = drop all (Q vs Ctrl+Q)
//   [Slot] item                     // id/daño/count del item soltado
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class DropItemPacket : public DataPacket {
public:
    uint8_t pid() const override { return DROP_ITEM_PACKET; }

    uint8_t type = 0;
    // Slot (ItemStack) — mismo formato que ContainerSetSlotPacket.
    int16_t itemId     = 0;
    uint8_t itemCount  = 0;
    int16_t itemDamage = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
