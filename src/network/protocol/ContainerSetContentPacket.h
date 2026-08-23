// SPDX-License-Identifier: LGPL-3.0-or-later
// ContainerSetContentPacket — MCPE protocol 84 packet stub.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cenisyspe::network::protocol {

class ContainerSetContentPacket : public DataPacket {
public:
    uint8_t pid() const override { return CONTAINER_SET_CONTENT_PACKET; }

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cenisyspe::network::protocol
