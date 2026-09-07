// SPDX-License-Identifier: LGPL-3.0-or-later
// ContainerClosePacket — bidirectional: client sends it when the player
// closes a container UI (X button, Esc, moving too far away); server
// sends it to force-close a UI (e.g. chest broken while open).
//
// PMMP wire format (protocol 84) — both encode() and decode() are just
// a single windowid byte (ContainerClosePacket.php, attached as design
// reference).
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace atomikpe::network::protocol {

class ContainerClosePacket : public DataPacket {
public:
    uint8_t pid() const override { return CONTAINER_CLOSE_PACKET; }

    uint8_t windowid = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace atomikpe::network::protocol
