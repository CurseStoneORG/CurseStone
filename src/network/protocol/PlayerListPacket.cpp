// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerListPacket.cpp — encode/decode.
// Genisys 0.15.0: putByte(type) + putInt(count) + per-entry fields (BE),
// where an ADD entry is uuid, entityId, name, skinId, skinData.
#include "PlayerListPacket.h"

namespace cursestone::network::protocol {

void PlayerListPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU8(type);
    w.writeI32BE(static_cast<int32_t>(entries.size()));
    for (const auto& e : entries) {
        w.writeBytes(e.uuid.data(), 16);
        if (type == TYPE_ADD) {
            w.writeI64BE(e.eid);
            w.writeBEString(e.name);
            w.writeBEString(e.skinId);
            w.writeBEString(e.skinData);
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
            if (!r.readBEString(e.skinId))   return false;
            if (!r.readBEString(e.skinData)) return false;
        }
        entries.push_back(std::move(e));
    }
    return true;
}

} // namespace cursestone::network::protocol
