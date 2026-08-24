// SPDX-License-Identifier: LGPL-3.0-or-later
// CraftingEventPacket — MCPE protocol 84 packet stub.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace atomikpe::network::protocol {

class CraftingEventPacket : public DataPacket {
public:
    uint8_t pid() const override { return CRAFTING_EVENT_PACKET; }

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace atomikpe::network::protocol
