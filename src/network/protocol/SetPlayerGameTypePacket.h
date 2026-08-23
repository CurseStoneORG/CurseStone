// SPDX-License-Identifier: LGPL-3.0-or-later
// SetPlayerGameTypePacket — MCPE protocol 84 packet stub.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace mcppeserver::network::protocol {

class SetPlayerGameTypePacket : public DataPacket {
public:
    uint8_t pid() const override { return SET_PLAYER_GAMETYPE_PACKET; }

    int32_t gamemode = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace mcppeserver::network::protocol
