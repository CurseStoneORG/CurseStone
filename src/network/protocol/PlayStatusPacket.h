// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayStatusPacket.h — server→client status signal during login/spawn.
//
// Wire format (protocol 84):
//   [1 byte ID 0x02]
//   [Int32 BE] status
//
// Status constants (mirror PMMP):
//   LOGIN_SUCCESS        = 0
//   LOGIN_FAILED_CLIENT  = 1  (client outdated)
//   LOGIN_FAILED_SERVER  = 2  (server outdated)
//   PLAYER_SPAWN         = 3  (tell client to show the world)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace mcppeserver::network::protocol {

class PlayStatusPacket : public DataPacket {
public:
    uint8_t pid() const override { return PLAY_STATUS_PACKET; }

    // Status enum (PMMP constants).
    static constexpr int32_t LOGIN_SUCCESS       = 0;
    static constexpr int32_t LOGIN_FAILED_CLIENT = 1;
    static constexpr int32_t LOGIN_FAILED_SERVER = 2;
    static constexpr int32_t PLAYER_SPAWN        = 3;

    int32_t status = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace mcppeserver::network::protocol
