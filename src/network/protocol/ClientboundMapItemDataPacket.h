// SPDX-License-Identifier: LGPL-3.0-or-later
// ClientboundMapItemDataPacket — MCPE protocol 84 packet stub.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace atomikpe::network::protocol {

class ClientboundMapItemDataPacket : public DataPacket {
public:
    uint8_t pid() const override { return CLIENTBOUND_MAP_ITEM_DATA; }

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace atomikpe::network::protocol
