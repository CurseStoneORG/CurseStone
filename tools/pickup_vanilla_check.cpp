// tools/pickup_vanilla_check.cpp
//
// Verifies the pickup packet layer against the reverse-engineered 0.15.10
// vanilla client (libminecraftpe.so, protocol 84, sha256 2d122090...):
//
// 1) PACKET IDS — RemoveEntityPacket 0x0C, AddItemEntityPacket 0x0D and
//    TakeItemEntityPacket 0x0E, read from the vanilla Packet::getId() Thumb
//    stubs (movs r0,#imm; bx lr) at 0x00aabb59 / 0x00aac925 / 0x00aac771.
// 2) TAKE WIRE FORMAT — vanilla TakeItemEntityPacket::write() (0x00aac775)
//    pushes two 64-bit values through RakNet::BitStream::WriteBits with the
//    BitStream::ReverseBytes network-order path, i.e. BIG-ENDIAN:
//    field1 = item entity unique id (resolved by the client with
//    Level::getEntity + EntityClassTree::isItemEntity), field2 = player
//    unique id (resolved with Level::getPlayer). Byte-exact comparison.
// 3) REAL PICKER ID — the three pickup routes (item scan, stuck arrows,
//    client-reported claim) must send the picker's own entity id in field2.
//    Level::getPlayer(0) — the legacy eid=0 self-addressed form — never
//    resolves and the client skips TakeAnimationParticle entirely, which is
//    why pickups rendered dry. Source scan over PlayerTick.cpp.
// 4) ADDITEMENTITY WIRE FORMAT — vanilla write (0x00aac928): one 64-bit eid
//    (no separate unique/runtime pair in protocol 84), PacketUtil::
//    writeItemInstance ([u16 id][u8 count][u16 aux][u16le nbtLen][nbt]) and
//    six 32-bit big-endian floats. Byte-exact comparison.
//
//   g++ -std=c++20 -Wall -Wextra -O1 -Isrc tools/pickup_vanilla_check.cpp src/network/protocol/TakeItemEntityPacket.cpp src/network/protocol/RemoveEntityPacket.cpp src/network/protocol/AddItemEntityPacket.cpp src/network/protocol/DataPacket.cpp -o /tmp/tools/pickup_vanilla_check
//
// Exit code 0 = every check passed; 1 = at least one check failed.
#include "network/protocol/AddItemEntityPacket.h"
#include "network/protocol/RemoveEntityPacket.h"
#include "network/protocol/TakeItemEntityPacket.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace cursestone::network::protocol;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++failures;
}
static std::string hex(const std::vector<uint8_t>& v) {
    static const char* d = "0123456789ABCDEF";
    std::string s;
    for (uint8_t b : v) {
        s += d[b >> 4];
        s += d[b & 0xF];
        s += ' ';
    }
    if (!s.empty())
        s.pop_back();
    return s;
}
static std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int main() {
    printf("pickup_vanilla_check: protocol-84 pickup packet layer\n");

    // 1) Packet ids — the values the vanilla client dispatches on.
    {
        printf("packet ids (vanilla Packet::getId stubs):\n");
        TakeItemEntityPacket take;
        RemoveEntityPacket rm;
        AddItemEntityPacket add;
        check(take.pid() == 0x0E, "TakeItemEntityPacket pid == 0x0E");
        check(rm.pid() == 0x0C, "RemoveEntityPacket pid == 0x0C");
        check(add.pid() == 0x0D, "AddItemEntityPacket pid == 0x0D");
    }

    // 2) Take wire format — [0x0E][I64BE item][I64BE player], byte exact.
    {
        printf("TakeItemEntityPacket wire format:\n");
        TakeItemEntityPacket take;
        take.target = (int64_t)0x00000000ABCD1234LL;   // field1: the item entity
        take.entityId = (int64_t)0x0000000012345678LL; // field2: the picking player
        std::vector<uint8_t> wire;
        take.writePacket(wire);

        const std::vector<uint8_t> expect = {
            0x0E,                                                   // Packet::getId() byte
            0x00, 0x00, 0x00, 0x00, 0xAB, 0xCD, 0x12, 0x34,        // item uid, big-endian
            0x00, 0x00, 0x00, 0x00, 0x12, 0x34, 0x56, 0x78,        // player uid, big-endian
        };
        check(wire.size() == expect.size() && std::memcmp(wire.data(), expect.data(), wire.size()) == 0,
              "encoded bytes match [0x0E][I64BE item][I64BE player]");
        if (wire.size() != expect.size() || std::memcmp(wire.data(), expect.data(), wire.size()) != 0)
            printf("    got:    %s\n    expect: %s\n", hex(wire).c_str(), hex(expect).c_str());
        check(wire.size() == 17 && wire[16] != 0x00,
              "field2 carries a nonzero (real) picker id");
    }

    // 3) The three pickup routes send the real picker id (source scan).
    {
        printf("pickup routes source scan:\n");
        std::string tick = slurp("src/player/PlayerTick.cpp");
        check(!tick.empty(), "src/player/PlayerTick.cpp readable (run from the repo root)");
        check(tick.find("entityId = 0") == std::string::npos,
              "no TakeItemEntityPacket entityId=0 self-addressed form remains");
        check(tick.find("pk_picker") == std::string::npos && tick.find("tk_self") == std::string::npos,
              "no duplicated picker-only packet builders remain");
        // One call per route (item scan, arrow scan, client-reported claim)
        // plus the definition line.
        size_t calls = 0;
        for (size_t pos = tick.find("sendPickupSequence("); pos != std::string::npos;
             pos = tick.find("sendPickupSequence(", pos + 1))
            ++calls;
        check(calls >= 4, "all three routes route through Player::sendPickupSequence");
        std::string header = slurp("src/Player.h");
        check(header.find("void sendPickupSequence(") != std::string::npos,
              "Player.h declares sendPickupSequence");
    }

    // 4) AddItemEntity wire format — [0x0D][I64BE eid][slot][6 x F32BE].
    {
        printf("AddItemEntityPacket wire format:\n");
        AddItemEntityPacket add;
        add.eid = (int64_t)0x0000000000001122LL;
        add.itemId = 345; // 0x0159
        add.itemCount = 5;
        add.itemDamage = 0;
        add.itemNbt.clear();
        add.x = 1.0f;
        add.y = 2.0f;
        add.z = 3.0f;
        add.speedX = add.speedY = add.speedZ = 0.0f;
        std::vector<uint8_t> wire;
        add.writePacket(wire);

        const std::vector<uint8_t> expect = {
            0x0D,                                            // Packet::getId() byte
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x22,  // eid, big-endian
            0x01, 0x59,                                      // item id, big-endian u16
            0x05,                                            // count, u8
            0x00, 0x00,                                      // aux/damage, big-endian u16
            0x00, 0x00,                                      // nbt length, little-endian u16
            0x3F, 0x80, 0x00, 0x00,                          // x = 1.0f, big-endian
            0x40, 0x00, 0x00, 0x00,                          // y = 2.0f
            0x40, 0x40, 0x00, 0x00,                          // z = 3.0f
            0x00, 0x00, 0x00, 0x00,                          // speedX
            0x00, 0x00, 0x00, 0x00,                          // speedY
            0x00, 0x00, 0x00, 0x00,                          // speedZ
        };
        check(wire.size() == expect.size() && std::memcmp(wire.data(), expect.data(), wire.size()) == 0,
              "encoded bytes match [0x0D][I64BE eid][slot][6xF32BE]");
        if (wire.size() != expect.size() || std::memcmp(wire.data(), expect.data(), wire.size()) != 0)
            printf("    got:    %s\n    expect: %s\n", hex(wire).c_str(), hex(expect).c_str());
    }

    printf("%s\n", failures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
