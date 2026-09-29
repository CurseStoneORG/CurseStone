// SPDX-License-Identifier: LGPL-3.0-or-later
// SetDifficultyPacket.cpp — encode/decode.
// PMMP: putInt(difficulty).
#include "SetDifficultyPacket.h"

namespace cursestone::network::protocol {

void SetDifficultyPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(difficulty);
}

bool SetDifficultyPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    return r.readI32BE(difficulty);
}

} // namespace cursestone::network::protocol
