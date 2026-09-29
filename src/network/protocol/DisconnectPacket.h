// SPDX-License-Identifier: LGPL-3.0-or-later
// DisconnectPacket.h — server tells the client "you have been kicked".
//
// Wire format (protocol 84):
//   [1 byte ID 0x05]
//   [BE String] message   (PMMP putString = putShort BE length + bytes)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>
#include <string>

namespace cursestone::network::protocol {

class DisconnectPacket : public DataPacket {
public:
    uint8_t pid() const override { return DISCONNECT_PACKET; }
    // Can be sent before login completes (e.g. protocol mismatch).
    bool canBeSentBeforeLogin() const override { return true; }

    std::string message;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
