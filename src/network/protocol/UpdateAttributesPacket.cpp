// SPDX-License-Identifier: LGPL-3.0-or-later
// UpdateAttributesPacket.cpp — encode/decode (protocol 84).
#include "UpdateAttributesPacket.h"

namespace cursestone::network::protocol {

void UpdateAttributesPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeVarU32((uint32_t)eid);
    w.writeVarU32((uint32_t)entries.size());
    for (const auto& e : entries) {
        // PMMP Attribute::encode: 4 little-endian floats + a varint-prefixed
        // string. The client (0.15.x) parses min/max/value/default as LE
        // despite the rest of the packet stream being BE.
        w.writeF32LE(e.min);
        w.writeF32LE(e.max);
        w.writeF32LE(e.value);
        w.writeF32LE(e.defaultValue);
        w.writeString(e.name);
    }
}

bool UpdateAttributesPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    uint32_t eid32 = 0;
    if (!r.readVarU32(eid32)) return false;
    eid = eid32;
    uint32_t count = 0;
    if (!r.readVarU32(count)) return false;
    entries.clear();
    for (uint32_t i = 0; i < count && !r.eof(); ++i) {
        Entry e;
        if (!r.readF32LE(e.min)) return false;
        if (!r.readF32LE(e.max)) return false;
        if (!r.readF32LE(e.value)) return false;
        if (!r.readF32LE(e.defaultValue)) return false;
        if (!r.readString(e.name)) return false;
        entries.push_back(std::move(e));
    }
    return true;
}

} // namespace cursestone::network::protocol
