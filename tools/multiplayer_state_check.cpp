// tools/multiplayer_state_check.cpp
//
// Regression checks for the three multiplayer state bugs reported
// against this server, validated byte-by-byte against Genisys 0.15.0
// (the reference implementation for MCPE 0.15.10 / protocol 84):
//
// 1) Respawn invisibility — Player::respawn() never re-announced the
//    player, unlike Genisys Player::respawn(), which ends with
//    `$this->spawnToAll();` and re-spawns nearby entities TO the
//    respawning client. Fix lives in Player.cpp; what is checkable here
//    is that the pieces it relies on encode the way Genisys writes them.
//
// 2) Broken blocks invisible to everyone else — UpdateBlockPacket was
//    emitting blockData and flags as TWO separate bytes. Genisys writes
//    `putByte(($flags << 4) | $data)` — ONE byte, flags in the high
//    nibble, data in the low nibble. With the old layout a break sent
//    blockData=0, so the receiving client derived flags = 0x0 =
//    FLAG_NONE: no neighbour/mesh rebuild, so the destroyed block stayed
//    on screen and stayed walk-through ("ghost block").
//
// 3) Crouch invisible to others — pose lives in entity metadata
//    property 0 (Entity::DATA_FLAGS, byte: bit 1 = SNEAKING,
//    bit 3 = SPRINTING) and is carried by SetEntityDataPacket, encoded
//    by Binary::writeMetadata as `(type << 5) | index` + value + 0x7F.
//    Genisys never actually sends it for sneaking (Player::setSneaking
//    is local-only), so the fix here is to send it, in the exact layout
//    Genisys would have used.
//
// Build recipe (verified working, exit 0):
//
//   g++ -std=c++20 -Wall -Wextra -O1 -Isrc -I/tmp/zstub
//       tools/multiplayer_state_check.cpp
//       src/network/protocol/UpdateBlockPacket.cpp
//       src/network/protocol/SetEntityDataPacket.cpp
//       src/network/protocol/PlayerActionPacket.cpp
//       src/network/protocol/DataPacket.cpp
//       src/utils/Binary.c
//       -o /tmp/tools/multiplayer_state_check
//
//   DataPacket.cpp is required at link time (PacketReader/PacketWriter live
//   there); Binary.c supplies the varint helpers it calls.
#include "network/protocol/UpdateBlockPacket.h"
#include "network/protocol/SetEntityDataPacket.h"
#include "network/protocol/PlayerActionPacket.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace cursestone::network::protocol;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static std::string hex(const std::vector<uint8_t>& v) {
    static const char* d = "0123456789ABCDEF";
    std::string s;
    for (uint8_t b : v) { s += d[b >> 4]; s += d[b & 0xF]; s += ' '; }
    return s;
}
static void dump(const char* label, const std::vector<uint8_t>& v) {
    printf("    %-26s %s\n", label, hex(v).c_str());
}

// ---- Genisys 0.15.0 UpdateBlockPacket::encode, transcribed literally ----
//     putInt(x); putInt(z); putByte(y); putByte(blockId);
//     putByte((flags << 4) | data);
static std::vector<uint8_t> genisysUpdateBlock(int32_t x, int32_t z, uint8_t y,
                                               uint8_t blockId, uint8_t data,
                                               uint8_t flags) {
    std::vector<uint8_t> o;
    o.push_back((uint8_t)(x >> 24)); o.push_back((uint8_t)(x >> 16));
    o.push_back((uint8_t)(x >> 8));  o.push_back((uint8_t)x);
    o.push_back((uint8_t)(z >> 24)); o.push_back((uint8_t)(z >> 16));
    o.push_back((uint8_t)(z >> 8));  o.push_back((uint8_t)z);
    o.push_back(y);
    o.push_back(blockId);
    o.push_back((uint8_t)(((flags & 0x0F) << 4) | (data & 0x0F)));
    return o;
}

// ---- Genisys 0.15.0 Binary::writeMetadata, transcribed literally ----
static std::vector<uint8_t> genisysMetadata(const std::vector<uint8_t>& keysAndBytes) {
    std::vector<uint8_t> o = keysAndBytes;
    o.push_back(0x7F);   // "\x7f" terminator
    return o;
}

int main() {
    printf("MULTIPLAYER STATE CHECK (Genisys 0.15.0 / protocol 84 reference)\n");

    // ================================================================
    printf("\n[1] UpdateBlockPacket — flags+data share ONE nibble-packed byte\n");
    // The exact case from the bug report: a block was broken.
    {
        UpdateBlockPacket p;                       // defaults: blockId 0, data 0
        p.x = 1234; p.z = -77; p.y = 64;
        p.blockId = 0; p.blockData = 0; p.flags = UpdateBlockPacket::FLAG_ALL_PRIORITY;
        std::vector<uint8_t> out; p.encode(out);
        auto want = genisysUpdateBlock(1234, -77, 64, 0, 0, UpdateBlockPacket::FLAG_ALL_PRIORITY);
        dump("break wire:", out);
        check(out.size() == 11, "break packet body is 11 bytes (4+4+1+1+1, not 12)");
        check(out == want, "break packet is byte-identical to Genisys");
        check(out[10] == 0xB0, "packed byte is 0xB0 (data=0, flags=FLAG_ALL_PRIORITY)");

        // THE regression: with the old two-byte layout the client read
        // blockData as the packed byte, deriving flags = 0x0 == FLAG_NONE.
        uint8_t flagsSeenByClient = (uint8_t)(out[10] >> 4);
        check(flagsSeenByClient != UpdateBlockPacket::FLAG_NONE,
              "client derives flags != FLAG_NONE (old bug sent 0x00)");
        check((flagsSeenByClient & UpdateBlockPacket::FLAG_NETWORK) != 0,
              "FLAG_NETWORK set (tells the client to rebuild mesh/collision)");
        check((flagsSeenByClient & UpdateBlockPacket::FLAG_NEIGHBORS) != 0,
              "FLAG_NEIGHBORS set (tells the client to update neighbours)");
    }
    // Placement with non-zero damage: both nibbles must survive together.
    {
        UpdateBlockPacket p;
        p.x = -1; p.z = -1; p.y = 255;
        p.blockId = 162;                 // WOODEN_LOG variants live here
        p.blockData = 13;                // damage lives in the LOW nibble
        p.flags = UpdateBlockPacket::FLAG_ALL_PRIORITY;
        std::vector<uint8_t> out; p.encode(out);
        auto want = genisysUpdateBlock(-1, -1, 255, 162, 13, UpdateBlockPacket::FLAG_ALL_PRIORITY);
        dump("place wire:", out);
        check(out == want, "place packet is byte-identical to Genisys");
        check(out[10] == 0xBD, "packed byte is 0xBD (data=13, flags=0x0B)");
        UpdateBlockPacket rt;
        check(rt.decode(out.data(), out.size()), "place packet decodes");
        check(rt.blockData == 13 && rt.flags == UpdateBlockPacket::FLAG_ALL_PRIORITY,
              "round-trip keeps data and flags separate");
        check(rt.x == -1 && rt.z == -1 && rt.y == 255 && rt.blockId == 162,
              "round-trip keeps x/z/y/blockId");
    }
    // Flag constant values must match Genisys UpdateBlockPacket.
    {
        check(UpdateBlockPacket::FLAG_NONE == 0,     "FLAG_NONE == 0");
        check(UpdateBlockPacket::FLAG_NEIGHBORS == 0b0001, "FLAG_NEIGHBORS == 0b0001");
        check(UpdateBlockPacket::FLAG_NETWORK == 0b0010,   "FLAG_NETWORK == 0b0010");
        check(UpdateBlockPacket::FLAG_NOGRAPHIC == 0b0100, "FLAG_NOGRAPHIC == 0b0100");
        check(UpdateBlockPacket::FLAG_PRIORITY == 0b1000,  "FLAG_PRIORITY == 0b1000");
        check(UpdateBlockPacket::FLAG_ALL == 0x03, "FLAG_ALL == 0x03");
        check(UpdateBlockPacket::FLAG_ALL_PRIORITY == 0x0B,
              "FLAG_ALL_PRIORITY == 0x0B (Genisys break/place default)");
    }

    // ================================================================
    printf("\n[2] SetEntityDataPacket — crouch/sprint pose flags\n");
    {
        // Crouched: DATA_FLAGS bit 1 -> value 0x02.
        SetEntityDataPacket p;
        p.eid = 0x0102030405060708LL;
        SetEntityDataPacket::Entry e;
        e.index = SetEntityDataPacket::PROP_FLAGS;
        e.type  = SetEntityDataPacket::TYPE_BYTE;
        e.ival  = 1 << SetEntityDataPacket::FLAG_BIT_SNEAKING;   // 0x02
        p.entries.push_back(e);

        std::vector<uint8_t> out; p.encode(out);

        // Genisys: putLong(eid) then chr((0 << 5) | 0) . chr(0x02) . "\x7f"
        std::vector<uint8_t> meta = genisysMetadata({ 0x00 /* key: type BYTE(0), index 0 */,
                                                     0x02 /* crouching */ });
        std::vector<uint8_t> want;
        for (int i = 7; i >= 0; --i) want.push_back((uint8_t)(0x0102030405060708LL >> (i * 8)));
        want.insert(want.end(), meta.begin(), meta.end());
        dump("sneak metadata:", out);
        check(out == want, "crouch metadata is byte-identical to Genisys writeMetadata");
        check(out.size() == 8 + 1 + 1 + 1, "body is eid(8) + key + value + terminator");
        check(out[8] == 0x00, "key byte 0x00 = DATA_FLAGS (index 0, type BYTE)");
        check(out[9] == 0x02, "value 0x02 = SNEAKING bit set");
        check(out[10] == 0x7F, "terminator 0x7F present");

        // Sprint uses bit 3 of the SAME property, so the full byte must be
        // resent; sending only one bit would clear the other on the client.
        check((1 << SetEntityDataPacket::FLAG_BIT_SPRINTING) == 0x08,
              "SPRINTING bit is 0x08 in DATA_FLAGS");
        check((1 << SetEntityDataPacket::FLAG_BIT_SNEAKING) == 0x02,
              "SNEAKING bit is 0x02 in DATA_FLAGS");
        check(SetEntityDataPacket::PROP_FLAGS == 0, "DATA_FLAGS property index is 0");

        SetEntityDataPacket rt;
        check(rt.decode(out.data(), out.size()), "metadata decodes");
        check(rt.entries.size() == 1, "decodes to exactly one entry");
        if (rt.entries.size() == 1) {
            check(rt.entries[0].index == 0 && rt.entries[0].type == 0,
                  "round-trip keeps index/type");
            check(rt.entries[0].ival == 0x02, "round-trip keeps the crouch bit");
        }
    }
    // eid 0 means "the receiving client's own entity" (Genisys sendData($this)).
    {
        SetEntityDataPacket p;
        p.eid = 0;
        SetEntityDataPacket::Entry e;
        e.index = SetEntityDataPacket::PROP_FLAGS;
        e.type  = SetEntityDataPacket::TYPE_BYTE;
        e.ival  = (1 << SetEntityDataPacket::FLAG_BIT_SNEAKING) |
                  (1 << SetEntityDataPacket::FLAG_BIT_SPRINTING);   // 0x0A
        p.entries.push_back(e);
        std::vector<uint8_t> out; p.encode(out);
        check(out[9] == 0x0A, "crouch+sprint together encode as 0x0A (single byte)");
        check(out.size() == 11, "self-metadata packet has the same shape");
    }

    // ================================================================
    printf("\n[3] PlayerActionPacket — sneak action ids (protocol 84)\n");
    {
        check(PlayerActionPacket::ACTION_START_SNEAK == 11, "ACTION_START_SNEAK == 11");
        check(PlayerActionPacket::ACTION_STOP_SNEAK  == 12, "ACTION_STOP_SNEAK == 12");
        check(PlayerActionPacket::ACTION_START_SPRINT == 9,  "ACTION_START_SPRINT == 9");
        check(PlayerActionPacket::ACTION_STOP_SPRINT  == 10, "ACTION_STOP_SPRINT == 10");
        check(PlayerActionPacket::ACTION_SPAWN_SAME_DIMENSION == 7,
              "ACTION_SPAWN_SAME_DIMENSION == 7");
        check(PlayerActionPacket::ACTION_START_BREAK == 0, "ACTION_START_BREAK == 0");
        // The ids must not collide with anything else handled server-side.
        check(PlayerActionPacket::ACTION_START_SNEAK != PlayerActionPacket::ACTION_START_SPRINT,
              "sneak ids do not collide with sprint ids");
    }

    printf("\nMULTIPLAYER STATE CHECK: %s (%d failure(s))\n",
           failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}