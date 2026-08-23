// SPDX-License-Identifier: LGPL-3.0-or-later
// ContainerSetContentPacket.cpp — encode (server→client).
//
// PMMP encode (verbatim):
//   putByte(windowid)
//   putShort(count(slots))
//   foreach slot: putSlot(slot)
//   if windowid == 0 && count(hotbar) > 0:
//       putShort(count(hotbar))
//       foreach hotbar: putInt(slot)
//   else:
//       putShort(0)
//
// putSlot: Int16 BE id; if id != 0: Uint8 count, Int16 BE damage.
#include "ContainerSetContentPacket.h"

namespace cenisyspe::network::protocol {

void ContainerSetContentPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    w.writeU8(windowid);
    w.writeU16BE(static_cast<uint16_t>(slots.size()));
    for (const auto& s : slots) {
        // PMMP putSlot: Int16 BE id; if id != 0: count + damage
        w.writeI16BE(static_cast<int16_t>(s.id));
        if (s.id != 0) {
            w.writeU8(static_cast<uint8_t>(s.count & 0xFF));
            w.writeI16BE(static_cast<int16_t>(s.meta));
        }
    }
    if (windowid == SPECIAL_INVENTORY && !hotbar.empty()) {
        w.writeU16BE(static_cast<uint16_t>(hotbar.size()));
        for (int32_t v : hotbar) w.writeI32BE(v);
    } else {
        w.writeU16BE(0);
    }
}

bool ContainerSetContentPacket::decode(const uint8_t* buf, size_t len) {
    PacketReader r(buf, len);
    if (!r.readU8(windowid)) return false;
    uint16_t n;
    if (!r.readU16BE(n)) return false;
    slots.clear();
    slots.reserve(n);
    for (uint16_t i = 0; i < n; ++i) {
        int16_t id;
        if (!r.readI16BE(id)) return false;
        cenisyspe::item::ItemStack s;
        s.id = static_cast<uint16_t>(id);
        if (id != 0) {
            uint8_t cnt;
            int16_t dmg;
            if (!r.readU8(cnt))   return false;
            if (!r.readI16BE(dmg)) return false;
            s.count = cnt;
            s.meta  = static_cast<uint8_t>(dmg & 0xFF);
        }
        slots.push_back(s);
    }
    uint16_t hn;
    if (!r.readU16BE(hn)) return false;
    hotbar.clear();
    hotbar.reserve(hn);
    for (uint16_t i = 0; i < hn; ++i) {
        int32_t v;
        if (!r.readI32BE(v)) return false;
        hotbar.push_back(v);
    }
    return true;
}

} // namespace cenisyspe::network::protocol
