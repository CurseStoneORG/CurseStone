// SPDX-License-Identifier: LGPL-3.0-or-later
// TakeItemEntityPacket implementation.
#include "TakeItemEntityPacket.h"

namespace cursestone::network::protocol {

void TakeItemEntityPacket::encode(std::vector<uint8_t>& out) const {
    // Format (DataPacket::writePacket already wrote the leading id byte):
    //   [Int64 BE target][Int64 BE entityId]
    //
    // BUG FIX: this used to write TAKE_ITEM_ENTITY_PACKET a SECOND time
    // as a payload byte, so the wire packet was
    //   [0x11][0x11][target][entityId]
    // Every field after the first was shifted one byte left and the
    // client decoded a garbage `target` entity id. The pickup animation
    // then targeted nothing: the item entity the player had just picked
    // up stayed rendered on the client (a ghost you can walk over but
    // that is already gone from the server's registry). encode() must
    // emit the BODY only — that is the contract every other packet in
    // this directory follows.
    PacketWriter w(out);
    w.writeI64BE(target);
    w.writeI64BE(entityId);
}

bool TakeItemEntityPacket::decode(const uint8_t* /*data*/, size_t /*len*/) {
    // Server→client only; no decode path needed.
    return true;
}

} // namespace cursestone::network::protocol
