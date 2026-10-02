// SPDX-License-Identifier: LGPL-3.0-or-later
// BlockEntityDataPacket — cliente→servidor cuando el jugador edita un
// cartel (al salir de la pantalla de texto). El servidor también lo usa
// para re-sincronizar el tile.
//
// Wire format (protocolo 84, mirrors Genisys BlockEntityDataPacket.php):
//   [Int BE] x
//   [Int BE] y
//   [Int BE] z
//   [rest]   namedtag      // árbol NBT little-endian del block entity
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class BlockEntityDataPacket : public DataPacket {
public:
    uint8_t pid() const override { return BLOCK_ENTITY_DATA_PACKET; }

    int32_t x = 0, y = 0, z = 0;
    std::vector<uint8_t> namedtag;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
