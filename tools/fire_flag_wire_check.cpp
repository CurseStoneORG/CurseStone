// Fire overlay wire check — validates the protocol-84 encoding of the
// on-fire pose flag CurseStone broadcasts when a player starts burning:
//   PROP_FLAGS (0), TYPE_BYTE, bit 0 set        -> key byte (0<<5)|0 = 0x00
//   payload byte 0x01                            -> burning
//   terminator 0x7F
// The 0.15 client renders the first-person fire overlay (the burn "atlas")
// off exactly this bit, and it only applies SetEntityDataPacket entries
// addressed to the entity id it has registered — the real runtime id, never
// the eid 0 alias. The check pins the bytes so a regression cannot slip
// through.
#include "network/protocol/SetEntityDataPacket.h"
#include "player/PlayerInternal.h"
#include <cstdio>
#include <vector>

using cursestone::network::protocol::SetEntityDataPacket;

int main() {
    // poseFlagByte: on-fire alone -> bit 0; combination keeps all bits.
    const uint8_t fire =
        cursestone::poseFlagByte(false, false, false, true, false);
    const uint8_t combo = cursestone::poseFlagByte(true, true, false, true, false);
    if (fire != 0x01) {
        std::printf("FAIL poseFlagByte(fire): got %02X want 01\n", fire);
        return 1;
    }
    if (combo != 0x0B) { // bits 0 | 1 | 3
        std::printf("FAIL poseFlagByte(combo): got %02X want 0B\n", combo);
        return 1;
    }

    SetEntityDataPacket pk;
    pk.eid = 0x0000736920736964LL; // arbitrary real runtime id

    SetEntityDataPacket::Entry flags;
    flags.index = SetEntityDataPacket::PROP_FLAGS; // 0
    flags.type = SetEntityDataPacket::TYPE_BYTE;
    flags.ival = fire; // bit 0 = burning
    pk.entries.push_back(flags);

    std::vector<uint8_t> out;
    pk.encode(out);

    const uint8_t expect[] = {
        0x00, 0x00, 0x73, 0x69, 0x20, 0x73, 0x69, 0x64, // eid (8B BE)
        0x00,                                           // key = (0<<5)|0
        0x01,                                           // byte value, bit 0 set
        0x7F,                                           // terminator
    };
    if (out.size() != sizeof(expect)) {
        std::printf("FAIL size: got %zu want %zu\n", out.size(), sizeof(expect));
        return 1;
    }
    for (size_t i = 0; i < sizeof(expect); ++i) {
        if (out[i] != expect[i]) {
            std::printf("FAIL byte %zu: got %02X want %02X\n", i, out[i], expect[i]);
            return 1;
        }
    }

    // Round-trip: the decoded entry must still read as "burning".
    SetEntityDataPacket back;
    if (!back.decode(out.data(), out.size())) {
        std::printf("FAIL decode\n");
        return 1;
    }
    if (back.entries.size() != 1 || back.entries[0].index != 0 ||
        back.entries[0].type != SetEntityDataPacket::TYPE_BYTE ||
        !(back.entries[0].ival & 0x01)) {
        std::printf("FAIL round-trip: burning bit lost\n");
        return 1;
    }

    std::printf("FIRE FLAG WIRE CHECK: OK\n");
    return 0;
}
