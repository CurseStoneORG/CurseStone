// SPDX-License-Identifier: LGPL-3.0-or-later
// SetTimePacket.h — sync world time to client.
//
// Wire format (protocol 84):
//   [1 byte ID 0x08]
//   [Int32 BE] time
//   [Uint8]    started (1 = time advancing, 0 = frozen)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class SetTimePacket : public DataPacket {
public:
    uint8_t pid() const override { return SET_TIME_PACKET; }

    int32_t time    = 0;
    bool    started = true;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
