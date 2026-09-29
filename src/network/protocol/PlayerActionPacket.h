// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerActionPacket.h — client→server action notification (start/abort/stop break).
//
// Wire format (protocol 84) — PMMP PlayerActionPacket::decode:
//   [1 byte ID 0x20]
//   [Int64 BE] eid       (legacy entity id; client echoes its own)
//   [Int32 BE] action    (0=START_BREAK, 1=ABORT_BREAK, 2=STOP_BREAK, ...)
//   [Int32 BE] x
//   [Int32 BE] y
//   [Int32 BE] z
//   [Int32 BE] face
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class PlayerActionPacket : public DataPacket {
public:
    uint8_t pid() const override { return PLAYER_ACTION_PACKET; }

    // Action constants (subset of PMMP PlayerAction:: constants relevant
    // for block breaking on protocol 84).
    static constexpr int32_t ACTION_START_BREAK = 0;
    static constexpr int32_t ACTION_ABORT_BREAK = 1;
    static constexpr int32_t ACTION_STOP_BREAK  = 2;
    // El cliente manda esto cuando el jugador toca el boton de
    // "Respawn" en la pantalla de muerte (protocolo legacy 84 — en
    // versiones modernas de Bedrock esto se maneja distinto).
    static constexpr int32_t ACTION_SPAWN_SAME_DIMENSION = 7;

    int64_t eid = 0;
    int32_t action = 0;
    int32_t x = 0, y = 0, z = 0;
    int32_t face = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
