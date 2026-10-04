// tools/take_item_wire_check.cpp
//
// Wire-format regression check for TakeItemEntityPacket (protocol 84).
//
// The packet used to write its own 1-byte id INSIDE encode(), on top of
// the one DataPacket::writePacket() already emits, so the client read
// `target` shifted one byte to the left — the pickup animation targeted a
// garbage entity id and the item the player had just collected stayed on
// screen as a ghost. encode() must emit the BODY only.
//
//   g++ -std=c++20 -Isrc tools/take_item_wire_check.cpp
//       src/network/protocol/TakeItemEntityPacket.cpp -o /tmp/take_item_wire
//   /tmp/take_item_wire
#include "network/protocol/TakeItemEntityPacket.h"

#include <cstdio>
#include <vector>

using namespace cursestone::network::protocol;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
    if (!ok) ++failures;
}

int main() {
    TakeItemEntityPacket pk;
    pk.target   = 0x0102030405060708LL;   // the dropped item
    pk.entityId = 0x1112131415161718LL;   // the player

    std::vector<uint8_t> out;
    pk.writePacket(out);                  // what Player::sendPacket() does

    std::printf("wire (%zu bytes):", out.size());
    for (uint8_t b : out) std::printf(" %02X", b);
    std::printf("\n");

    check(out.size() == 1 + 8 + 8, "17 bytes: [id][target i64 BE][entityId i64 BE]");
    check(out.size() > 0 && out[0] == TAKE_ITEM_ENTITY_PACKET, "leading byte is the packet id");
    check(out.size() > 1 && out[1] != TAKE_ITEM_ENTITY_PACKET,
          "body does NOT repeat the packet id (the bug)");

    auto be64 = [&](size_t off) {
        int64_t v = 0;
        for (size_t i = 0; i < 8; ++i) v = (int64_t)((uint64_t)v << 8) | out[off + i];
        return v;
    };
    check(be64(1) == pk.target,   "target decodes to the dropped item's eid");
    check(be64(9) == pk.entityId, "entityId decodes to the picker's eid");

    std::printf("\n%s (%d failure(s))\n", failures ? "TAKE ITEM WIRE: FAIL" : "TAKE ITEM WIRE: OK",
                failures);
    return failures ? 1 : 0;
}