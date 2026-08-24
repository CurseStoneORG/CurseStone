// SPDX-License-Identifier: LGPL-3.0-or-later
// SetPlayerGameTypePacket — MCPE protocol 84 packet.
//
// Wire format (protocol 84):
//   [1 byte ID 0x21]
//   [Int32 BE] gamemode  (0 = survival, 1 = creative, 2 = adventure)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace atomikpe::network::protocol {

class SetPlayerGameTypePacket : public DataPacket {
public:
    uint8_t pid() const override { return SET_PLAYER_GAMETYPE_PACKET; }

    int32_t gamemode = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace atomikpe::network::protocol
