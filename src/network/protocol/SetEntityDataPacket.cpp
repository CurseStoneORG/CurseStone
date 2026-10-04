// SPDX-License-Identifier: LGPL-3.0-or-later
// SetEntityDataPacket.cpp — encode/decode.
//
// Genisys 0.15.0:
//     SetEntityDataPacket::encode:  putLong(eid); put(Binary::writeMetadata(meta))
//     Binary::writeMetadata:        for each entry
//                                      chr((type << 5) | (index & 0x1F))
//                                      <value>
//                                  then "\x7f"
#include "SetEntityDataPacket.h"

namespace cursestone::network::protocol {

void SetEntityDataPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeI64BE(eid);
    for (const Entry& e : entries) {
        // Key byte: value type in the top 3 bits, property index in the low 5.
        w.writeU8((uint8_t)(((e.type & 0x07) << 5) | (e.index & 0x1F)));
        switch (e.type) {
            case TYPE_BYTE:   w.writeU8((uint8_t)(e.ival & 0xFF));      break;
            case TYPE_SHORT:  w.writeU16BE((uint16_t)(e.ival & 0xFFFF)); break;
            case TYPE_INT:    w.writeI32BE((int32_t)e.ival);             break;
            case TYPE_LONG:   w.writeI64BE(e.ival);                     break;
            case TYPE_FLOAT:  w.writeF32BE(e.fval);                     break;
            case TYPE_STRING: w.writeBEString(e.sval);                  break;
            default: break;   // TYPE_POS / unsupported: nothing on the wire
        }
    }
    w.writeU8(TERMINATOR);
}

bool SetEntityDataPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    entries.clear();
    if (!r.readI64BE(eid)) return false;
    while (!r.eof()) {
        uint8_t key = 0;
        if (!r.readU8(key)) return false;
        if (key == TERMINATOR) return true;   // 0x7F == type 7/index 31 = end
        Entry e;
        e.type  = (uint8_t)((key >> 5) & 0x07);
        e.index = (uint8_t)(key & 0x1F);
        switch (e.type) {
            case TYPE_BYTE: {
                uint8_t v = 0;
                if (!r.readU8(v)) return false;
                e.ival = v;
                break;
            }
            case TYPE_SHORT: {
                uint16_t v = 0;
                if (!r.readU16BE(v)) return false;
                e.ival = v;
                break;
            }
            case TYPE_INT: {
                int32_t v = 0;
                if (!r.readI32BE(v)) return false;
                e.ival = v;
                break;
            }
            case TYPE_LONG: {
                int64_t v = 0;
                if (!r.readI64BE(v)) return false;
                e.ival = v;
                break;
            }
            case TYPE_FLOAT: {
                float v = 0.0f;
                if (!r.readF32BE(v)) return false;
                e.fval = v;
                break;
            }
            case TYPE_STRING: {
                if (!r.readBEString(e.sval)) return false;
                break;
            }
            default:
                // Unknown type: we cannot know its width, so stop cleanly
                // rather than emitting garbage into `entries`.
                return true;
        }
        entries.push_back(std::move(e));
    }
    return true;
}

} // namespace cursestone::network::protocol