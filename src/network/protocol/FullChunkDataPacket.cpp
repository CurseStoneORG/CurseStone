// SPDX-License-Identifier: LGPL-3.0-or-later
// FullChunkDataPacket.cpp — encode/decode.
//
// PMMP: putInt(chunkX) + putInt(chunkZ) + putByte(order) + putInt(strlen(data)) + put(data)
// All ints are BE.
#include "FullChunkDataPacket.h"

namespace cursestone::network::protocol {

void FullChunkDataPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI32BE(chunkX);
    w.writeI32BE(chunkZ);
    w.writeU8 (order);
    w.writeI32BE(static_cast<int32_t>(data.size()));
    w.writeBytes(data.data(), data.size());
}

bool FullChunkDataPacket::decode(const uint8_t* buf, size_t len) {
    PacketReader r(buf, len);
    int32_t dataLen;
    if (!r.readI32BE(chunkX))      return false;
    if (!r.readI32BE(chunkZ))      return false;
    if (!r.readU8 (order))         return false;
    if (!r.readI32BE(dataLen))     return false;
    if (dataLen < 0)               return false;
    size_t need = static_cast<size_t>(dataLen);
    if (need > r.remaining())     return false;
    data.resize(need);
    return r.readBytes(data.data(), need);
}

} // namespace cursestone::network::protocol
