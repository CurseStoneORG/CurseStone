// tools/playerlist_wire_check.cpp
//
// The multiplayer crash: when a second player joined, BOTH 0.15.10
// clients died within ~2 seconds. The only thing sent at that moment is
// the pair PlayerListPacket(ADD) + AddPlayerPacket describing the other
// player. Genisys 0.15.0 (the reference for protocol 84) writes the
// player-list entry as
//
//     [u8 type][i32BE count]{ [16B uuid][i64BE eid][str name]
//                             [str skinId][str skinData] }     <- ADD
//     [16B uuid]                                                    <- REMOVE
//
// while its own class comment claims "isSlim, skin". The server used to
// follow the comment and sent "false" + an EMPTY skin blob — which the
// client cannot load, and dies on. Genisys' own Human::spawnTo() refuses
// to spawn an entity whose skin is shorter than 64*32*4 bytes for the
// same reason.
//
// This checks the bytes on the wire against that layout, that a decode
// round-trips, and that the skin blob we fall back to is a real, loadable
// 64x64 skin (never an empty string).
//
//   g++ -std=c++20 -Wall -Wextra -O1 -Isrc -I/tmp/zstub
//       tools/playerlist_wire_check.cpp src/network/protocol/PlayerListPacket.cpp
//       -o /tmp/tools/playerlist_wire_check
#include "network/protocol/PlayerListPacket.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace cursestone::network::protocol;

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// --- minimal base64 decoder (the blob must be decodable by the client) ---
static std::string base64Decode(const std::string& in) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n') continue;
        const char* p = strchr(tbl, c);
        if (!p) continue;
        val = (val << 6) | (int)(p - tbl);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((char)((val >> bits) & 0xFF));
        }
    }
    return out;
}

// The default skin Player.cpp falls back to, byte-for-byte.
static std::string defaultSkinData() {
    return std::string(
        "{\"skinId\":\"Standard_Custom\",\"data\":\""
        "iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAAZUlEQVR42u3QQREAAAQAMHnFkUkv"
        "cjh7rMCiK+ezECBAgAABAgQIECBAgAABAgQIECBAgAABAgQIECBAgAABAgQIECBAgAABAgQIECBA"
        "gAABAgQIECBAgAABAgQIECBAgAABAgQIECBAgAABAu5bsTTyhgtuRSIAAAAASUVORK5CYII="
        "\",\"geometryName\":\"geometry.humanoid.customGeometry1\"}");
}

int main() {
    printf("1. PlayerListPacket ADD layout vs Genisys 0.15.0\n");
    PlayerListPacket pk;
    pk.type = PlayerListPacket::TYPE_ADD;
    PlayerListPacket::Entry e;
    for (int i = 0; i < 16; ++i) e.uuid[i] = (uint8_t)(i + 1);
    e.eid   = 0x0102030405060708LL;
    e.name  = "Dr1xy";
    e.skinId   = "Standard_Skin_1";
    e.skinData = defaultSkinData();
    pk.entries.push_back(e);

    std::vector<uint8_t> wire;
    pk.encode(wire);

    // Walk the buffer exactly like the client does. Strings use
    // putString = uint16 BE length (Genisys 0.15.0 BinaryStream::putString).
    size_t o = 0;
    check(o < wire.size() && wire[o++] == PlayerListPacket::TYPE_ADD, "type byte is ADD (0)");
    uint32_t count = (uint32_t)wire[o] << 24 | (uint32_t)wire[o+1] << 16 |
                     (uint32_t)wire[o+2] << 8 | wire[o+3];
    o += 4;
    check(count == 1, "entry count is a BE int32 = 1");

    bool uuidOk = true;
    for (int i = 0; i < 16; ++i) if (wire[o + i] != (uint8_t)(i + 1)) uuidOk = false;
    o += 16;
    check(uuidOk, "16 raw UUID bytes come first");

    uint64_t eid = 0;
    for (int i = 0; i < 8; ++i) eid = (eid << 8) | wire[o + i];
    o += 8;
    check(eid == (uint64_t)e.eid, "entity id is an 8-byte BE long");

    auto readBEstring = [&](std::string& out) {
        const uint32_t len = (uint32_t)wire[o] << 8 | wire[o+1];
        o += 2;
        out.assign((const char*)wire.data() + o, len);
        o += len;
    };
    std::string name, skinId, skinData;
    readBEstring(name);
    readBEstring(skinId);
    readBEstring(skinData);

    check(name == "Dr1xy", "name is the 3rd field");
    check(skinId == "Standard_Skin_1", "4th field is the SKIN ID (not \"false\")");
    check(!skinData.empty(), "5th field (skin data) is NOT empty");
    check(o == wire.size(), "the packet ends exactly after the 5 fields");

    printf("2. decode round-trip\n");
    PlayerListPacket back;
    check(back.decode(wire.data(), wire.size()), "the packet decodes");
    check(back.entries.size() == 1 && back.entries[0].name == name &&
          back.entries[0].skinId == skinId &&
          back.entries[0].skinData == skinData && back.entries[0].eid == e.eid,
          "every field survives the round-trip");

    printf("3. REMOVE entry is uuid only (Genisys)\n");
    PlayerListPacket rm;
    rm.type = PlayerListPacket::TYPE_REMOVE;
    rm.entries.push_back(e);
    std::vector<uint8_t> rwire;
    rm.encode(rwire);
    check(rwire.size() == 1u + 4u + 16u,
          "REMOVE writes type + count + 16 UUID bytes and nothing else");

    printf("4. the fallback skin is a real, loadable 64x64 skin\n");
    const std::string skin = defaultSkinData();
    const size_t q = skin.find("\"data\":\"");
    check(q != std::string::npos, "skin blob carries a data field");
    const std::string b64 = skin.substr(q + 8, skin.find('"', q + 8) - (q + 8));
    const std::string png = base64Decode(b64);
    check(png.size() > 8 && memcmp(png.data(), "\x89PNG\r\n\x1a\n", 8) == 0,
          "base64 decodes to a PNG");
    // PNG layout: 8B signature, 4B chunk length, "IHDR", then width/height
    // as 4-byte BE each — 8..11 length, 12..15 type, 16..19 width, 20..23 height.
    check(png.size() >= 24 &&
          png[16] == 0 && png[17] == 0 && png[18] == 0 && png[19] == 64 &&
          png[20] == 0 && png[21] == 0 && png[22] == 0 && png[23] == 64,
          "the PNG is 64x64 (what a 0.15 skin texture must be)");
    check(skin.find("geometryName") != std::string::npos,
          "skin blob carries geometryName");

    printf("\n%s (%d failure(s))\n",
           failures ? "PLAYERLIST WIRE: FAIL" : "PLAYERLIST WIRE: OK", failures);
    return failures ? 1 : 0;
}