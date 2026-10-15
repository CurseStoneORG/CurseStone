// Sleep metadata wire check — validates the protocol-84 encoding of the
// sleeping property pair CurseStone sends via SetEntityDataPacket:
//   PROP_PLAYER_FLAGS (16), TYPE_BYTE, value 1  → key byte (0<<5)|16 = 0x10
//   PROP_PLAYER_BED_POSITION (17), TYPE_POS     → key byte (6<<5)|17 = 0xD1
//       followed by three packed int32 BE (x, y, z)
//   terminator 0x7F
// Reference: Infinity Human.php:50-55 (indices 16/17) and PMMP 1.6.2
// Binary::writeMetadata DATA_TYPE_POS (3 x pack("N")).
#include "network/protocol/SetEntityDataPacket.h"
#include <cassert>
#include <cstdio>
#include <vector>

using cursestone::network::protocol::SetEntityDataPacket;

int main() {
    SetEntityDataPacket pk;
    pk.eid = 0x0102030405060708LL;

    SetEntityDataPacket::Entry flags;
    flags.index = SetEntityDataPacket::PROP_PLAYER_FLAGS;      // 16
    flags.type  = SetEntityDataPacket::TYPE_BYTE;
    flags.ival  = SetEntityDataPacket::PLAYER_FLAG_SLEEP;      // 1
    pk.entries.push_back(flags);

    SetEntityDataPacket::Entry bed;
    bed.index = SetEntityDataPacket::PROP_PLAYER_BED_POSITION; // 17
    bed.type  = SetEntityDataPacket::TYPE_POS;
    bed.px = 128; bed.py = 65; bed.pz = -42;
    pk.entries.push_back(bed);

    std::vector<uint8_t> out;
    pk.encode(out);

    // Expected: eid (8B BE) + 0x10 01 + 0xD1 + x/y/z int32BE + 0x7F
    const uint8_t expect[] = {
        0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,   // eid
        (uint8_t)((0 << 5) | 16), 0x01,            // flags byte property = 1
        (uint8_t)((6 << 5) | 17),                  // POS property key
        0x00,0x00,0x00,0x80,                       // x = 128
        0x00,0x00,0x00,0x41,                       // y = 65
        0xFF,0xFF,0xFF,0xD6,                       // z = -42
        0x7F,                                      // terminator
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

    // Round-trip: decode our own bytes and confirm the fields come back.
    SetEntityDataPacket back;
    if (!back.decode(out.data(), out.size())) { std::printf("FAIL decode\n"); return 1; }
    assert(back.entries.size() == 2);
    bool ok = back.entries[0].index == 16 && back.entries[0].ival == 1 &&
              back.entries[1].index == 17 && back.entries[1].type == SetEntityDataPacket::TYPE_POS &&
              back.entries[1].px == 128 && back.entries[1].py == 65 && back.entries[1].pz == -42;
    if (!ok) { std::printf("FAIL round-trip fields\n"); return 1; }

    std::printf("SLEEP METADATA WIRE CHECK: OK\n");
    return 0;
}
