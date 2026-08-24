// SPDX-License-Identifier: LGPL-3.0-or-later
// SetHealthPacket.h — sync player health to client.
//
// Wire format (protocol 84):
//   [1 byte ID 0x25]
//   [Int32 BE] health (0..20)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace atomikpe::network::protocol {

class SetHealthPacket : public DataPacket {
public:
    uint8_t pid() const override { return SET_HEALTH_PACKET; }

    int32_t health = 20;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace atomikpe::network::protocol
