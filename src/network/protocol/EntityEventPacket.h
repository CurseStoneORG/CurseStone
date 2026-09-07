// SPDX-License-Identifier: LGPL-3.0-or-later
// EntityEventPacket — clientbound. Dispara animaciones/eventos
// cliente-side atados a una entidad (golpe recibido, muerte, etc).
// Wire format (protocol 84, mirrors PMMP EntityEventPacket):
//   [Int64 BE] eid
//   [Byte]     event
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace atomikpe::network::protocol {

class EntityEventPacket : public DataPacket {
public:
    uint8_t pid() const override { return ENTITY_EVENT_PACKET; }

    // Solo se usan los eventos que el pipeline de combate actual
    // dispara de verdad — el resto de la tabla de PMMP (tame,
    // fish-hook, ambient sound, etc.) no aplica todavia porque esas
    // mecanicas no existen en AtomikPE.
    static constexpr uint8_t HURT_ANIMATION  = 2;
    static constexpr uint8_t DEATH_ANIMATION = 3;

    int64_t eid = 0;
    uint8_t event = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace atomikpe::network::protocol
