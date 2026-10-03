// SPDX-License-Identifier: LGPL-3.0-or-later
// UpdateAttributesPacket.cpp — encode/decode (protocol 84).
//
// Wire format mirrors Infinity (PMMP 0.15.10 lineage) BYTE-FOR-BYTE:
//   [Long BE]  entityId    — 8 bytes (Binary::writeLong = pack("NN", hi, lo))
//   [Short BE] entry count — 2 bytes
//   per entry:
//     [Float BE] min
//     [Float BE] max
//     [Float BE] value
//     [String]   name — Short BE length + bytes (BinaryStream::putString)
//
// CAUTION: protocol 84 has NO default-value float (that field appeared in
// later protocols) and NO varints anywhere in this packet (varints arrived
// in newer MCPE protocols). Encoding varints / little-endian floats here
// desyncs the client's packet parser — the app hangs on the "Generating
// terrain" screen and then crashes ("Minecraft se detuvo").
#include "UpdateAttributesPacket.h"

namespace cursestone::network::protocol {

void UpdateAttributesPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU64BE(eid);                        // putLong — BE int64
    w.writeU16BE((uint16_t)entries.size());   // putShort — BE uint16
    for (const auto& e : entries) {
        w.writeF32BE(e.min);                  // putFloat — BE float
        w.writeF32BE(e.max);                  // putFloat — BE float
        w.writeF32BE(e.value);                // putFloat — BE float
        w.writeBEString(e.name);              // putString — Short BE length + bytes
    }
}

bool UpdateAttributesPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readU64BE(eid)) return false;
    uint16_t count = 0;
    if (!r.readU16BE(count)) return false;
    entries.clear();
    for (uint16_t i = 0; i < count && !r.eof(); ++i) {
        Entry e;
        if (!r.readF32BE(e.min)) return false;
        if (!r.readF32BE(e.max)) return false;
        if (!r.readF32BE(e.value)) return false;
        if (!r.readBEString(e.name)) return false;
        entries.push_back(std::move(e));
    }
    return true;
}

} // namespace cursestone::network::protocol
