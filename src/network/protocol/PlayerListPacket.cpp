// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerListPacket.cpp — encode/decode.
// PMMP: putByte(type) + putInt(count) + per-entry fields (BE).
#include "PlayerListPacket.h"

namespace mcppeserver::network::protocol {

void PlayerListPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU8(type);
    w.writeI32BE(static_cast<int32_t>(entries.size()));
    for (const auto& e : entries) {
        w.writeBytes(e.uuid.data(), 16);
        if (type == TYPE_ADD) {
            w.writeI64BE(e.eid);
            w.writeBEString(e.name);
            w.writeBEString(e.isSlim);
            w.writeBEString(e.skin);
        }
    }
}

bool PlayerListPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    int32_t count;
    if (!r.readU8(type))        return false;
    if (!r.readI32BE(count))    return false;
    if (count < 0)              return false;
    entries.clear();
    entries.reserve(count);
    for (int32_t i = 0; i < count; ++i) {
        Entry e;
        if (r.remaining() < 16) return false;
        if (!r.readBytes(e.uuid.data(), 16)) return false;
        if (type == TYPE_ADD) {
            if (!r.readI64BE(e.eid))   return false;
            if (!r.readBEString(e.name))  return false;
            if (!r.readBEString(e.isSlim)) return false;
            if (!r.readBEString(e.skin))  return false;
        }
        entries.push_back(std::move(e));
    }
    return true;
}

} // namespace mcppeserver::network::protocol
