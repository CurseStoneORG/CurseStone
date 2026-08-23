// SPDX-License-Identifier: LGPL-3.0-or-later
// TextPacket.h — chat / popup / tip / system message.
//
// Wire format (protocol 84):
//   [1 byte ID 0x07]
//   [Uint8] type
//   if (type == CHAT or POPUP): [BE String] source
//   if (type != TRANSLATION):   [BE String] message
//   if (type == TRANSLATION):   [BE String] message, [Uint8] count, [BE String]*count
//
// Type constants (mirror PMMP):
//   TYPE_RAW        = 0
//   TYPE_CHAT       = 1   (requires `source`)
//   TYPE_TRANSLATION= 2
//   TYPE_POPUP      = 3  (requires `source`)
//   TYPE_TIP        = 4
//   TYPE_SYSTEM     = 5
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>
#include <string>

namespace cenisyspe::network::protocol {

class TextPacket : public DataPacket {
public:
    uint8_t pid() const override { return TEXT_PACKET; }

    static constexpr uint8_t TYPE_RAW         = 0;
    static constexpr uint8_t TYPE_CHAT        = 1;
    static constexpr uint8_t TYPE_TRANSLATION = 2;
    static constexpr uint8_t TYPE_POPUP       = 3;
    static constexpr uint8_t TYPE_TIP         = 4;
    static constexpr uint8_t TYPE_SYSTEM      = 5;

    uint8_t     type = TYPE_RAW;
    std::string source;                       // CHAT/POPUP only
    std::string message;
    std::vector<std::string> parameters;      // TRANSLATION only

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cenisyspe::network::protocol
