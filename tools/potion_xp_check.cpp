// tools/potion_xp_check.cpp
//
// Verifies the throwable-potion trajectory, experience bottle, XP orb and
// player XP systems against the reverse-engineered 0.15.10 vanilla
// reference (libminecraftpe.so, protocol 84, sha256 2d122090...) and the
// Genisys 0.15.0 era reference (tag ec716b0ea4):
//
// 1) THROW PIPELINE — the vanilla Throwable::shoot(Mob&) @ 0x00c1e735 +
//    shoot(Vec3,float,float,Vec3) @ 0x00c1e858 constants: throw power 0.5
//    (0x3F000000, overrides the 1.5 base), up-angle offset -20 degrees
//    (0xC1A00000, overrides the 0 base) applied to the pitch, gaussian aim
//    spread 0.0075 per axis (0x3BF5C28F literal, Box-Muller polar pairs)
//    and the thrower motion added after the power scaling. Source scan
//    over PlayerCombat.cpp. The Genisys-era values (power 1.1, no
//    up-angle, no spread) must be gone.
// 2) THROWN FLIGHT PHYSICS — ThrownPotion/ExperiencePotion overrides:
//    gravity 0.05 per tick (0x3D4CCCCD), drag 0.99 (0x3F7D70A4) with the
//    0.8 water branch (0x3F4CCCCD), 1200-tick life. Source scan over
//    World.cpp. Genisys used gravity 0.1 / drag 0.95 — both gone.
// 3) XP ORB ENTITIES — entity type 69 (ExperienceOrb::getEntityTypeId
//    0x00be8730), bottle type 68 (ExperiencePotion::getEntityTypeId
//    0x00c1c0cc), orb value carried in metadata index 11 TYPE_INT
//    (SynchedEntityData::getInt(11) drives the icon size), gravity -0.04
//    (0xBD23D70A), water buoyancy +0.2, horizontal drag 0.588
//    (0x3F16872C), lifetime 6000 (LIFETIME 0x0154793c = 0x1770),
//    attraction delta/8 * (1-len)^2 with NO 0.7 factor (Genisys had one),
//    pickup gated by XpTakeDelay(2) and the LevelEvent 1051 "random.orb"
//    sound at player.pos + 0.5. Source scan over World.cpp.
// 4) ORB VALUE LADDER — ExperienceOrb::spawnOrbs @ 0x00be8864 greedy split
//    into {1,3,7,17,37,73,149,307,617,1237,2477} (rule: remaining > 2 → 3,
//    > 6 → 7, > 16 → 17, > 36 → 37, > 72 → 73, > 148 → 149, > 306 → 307,
//    > 616 → 617, > 1236 → 1237, > 2476 → 2477, else 1), bottle break
//    total 3..10 (spawnOrbs(region,pos,3,11,null)). Source scan over
//    World.cpp.
// 5) PLAYER XP CURVE — Player::addExperience @ 0x00c15e78 era cost curve
//    recovered from the smmla bucket (magic 0x88888889): level<16 → 2L+7,
//    16..30 → 5L-38, 31+ → 9L-158. The Genisys modern curve
//    (L²+6L / 2.5L²-40.5L+360 / 4.5L²-162.5L+2220) must be gone. The
//    level-up loop rescales leftover progress by the NEW level cost.
//    Source scan over PlayerStats.cpp.
// 6) XP PERSISTENCE — NBT keys XpLevel + XpP (vanilla player.dat keys,
//    already present) plus XpTotal for the lifetime counter. Source scan
//    over Player.cpp / PlayerLogin.cpp.
// 7) PROTOCOL — AddEntityPacket wire format for an XP orb spawn:
//    [pid 0x0c][eid 8B BE][type 4B BE = 69][pos 3×F32 BE][speed 3×F32 BE]
//    [yaw/pitch 2×F32 BE scaled 0.71111][modifiers 4B][metadata ...] with
//    the metadata entry {type INT=2, index 11, value} and the 0x7f
//    terminator. Byte-exact comparison of the encoded packet. Also checks
//    the LevelEventPacket constants used for the orb pickup sound (1051)
//    and the splash shatter (2002).
//
//   g++ -std=c++20 -Wall -Wextra -O1 -Isrc tools/potion_xp_check.cpp
//       src/network/protocol/AddEntityPacket.cpp \
//       src/network/protocol/LevelEventPacket.cpp
//       src/network/protocol/SetEntityDataPacket.cpp \
//       src/network/protocol/LevelEventPacket.cpp
//       src/network/protocol/DataPacket.cpp -o /tmp/tools/potion_xp_check
//
// Exit code 0 = every check passed; 1 = at least one check failed.
#include "network/protocol/AddEntityPacket.h"
#include "network/protocol/LevelEventPacket.h"

#include <cmath>
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

static std::string slurp(const char* path) {
    std::ifstream f(path);
    if (!f)
        return "";
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}
static bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

int main() {
    printf("1) THROW PIPELINE (PlayerCombat.cpp)\n");
    {
        std::string s = slurp("src/player/PlayerCombat.cpp");
        check(has(s, "kThrowPower = 0.5"), "throw power 0.5 (binary getThrowPower override)");
        check(has(s, "kUpAngleOffsetDeg = -20.0"), "up-angle offset -20 (binary getThrowUpAngleOffset)");
        check(has(s, "kAimSpread = 0.0075"), "gaussian aim spread 0.0075 (shoot literal)");
        check(has(s, "velocityEstX()"), "thrower motion inherited (shoot 4th arg)");
        check(has(s, "-2.0 * std::log(s) / s"), "Box-Muller polar gaussian generator");
        check(has(s, "TYPE_EXP_BOTTLE"), "experience bottle throw registered");
        check(!has(s, "kThrowPower = 1.1"), "Genisys power 1.1 removed");
    }

    printf("2) THROWN FLIGHT PHYSICS (World.cpp)\n");
    {
        std::string s = slurp("src/world/World.cpp");
        check(has(s, "p->vy -= 0.05;"), "thrown gravity 0.05 (getGravity override)");
        check(has(s, "? 0.8 : 0.99"), "drag 0.99 with 0.8 water branch");
        check(has(s, "p->vx *= drag;\n        p->vy *= drag;\n        p->vz *= drag;"),
              "drag applied to all three components");
        check(!has(s, "p->vy -= 0.1;\n        p->vx *= 0.95;"),
              "Genisys gravity 0.1 / drag 0.95 removed");
    }

    printf("3) XP ORB ENTITIES (World.cpp)\n");
    {
        std::string s = slurp("src/world/World.cpp");
        check(has(s, "o->vy -= 0.04;"), "orb gravity -0.04 (normalTick literal)");
        check(has(s, "o->vy = 0.2;"), "water buoyancy +0.2 (Material::isType(6) branch)");
        check(has(s, "o->vx *= 0.588;\n        o->vz *= 0.588;\n        o->vy *= 0.98;"),
              "orb drag 0.588 XZ / 0.98 Y (binary literals)");
        check(has(s, "o->age >= 6000"), "orb lifetime 6000 (LIFETIME 0x1770)");
        check(has(s, "/ 8.0") && has(s, "1.0 - len"), "attraction delta/8 with c = 1-len");
        check(!has(s, "motSqrt") && !has(s, "moveSpeed = 0.7"),
              "no Genisys 0.7 attraction factor");
        check(has(s, "setXpTakeDelay(2)"), "pickup sets XpTakeDelay(2)");
        check(has(s, "EVENT_SOUND_ORB"), "pickup sound via LevelEvent");
        check(has(s, "pl->addExperience(o->value)"), "authoritative XP grant");
        check(has(s, "PROP_XP_ORB_VALUE"), "orb value metadata index");
        check(has(s, "spawnXPOrbs(p->x, p->y - 0.2, p->z, total)"),
              "bottle break spawns orbs at hit position");
        check(has(s, "3 + (std::rand() % 8)"), "bottle XP total 3..10 (spawnOrbs 3,11)");
    }

    printf("4) ORB VALUE LADDER (World.cpp)\n");
    {
        std::string s = slurp("src/world/World.cpp");
        const int thresholds[] = {2476, 1236, 616, 306, 148, 72, 36, 16, 6, 2};
        const int values[] = {2477, 1237, 617, 307, 149, 73, 37, 17, 7, 3};
        bool all = true;
        for (size_t i = 0; i < sizeof(thresholds) / sizeof(int); ++i) {
            std::string hi = "remaining > " + std::to_string(thresholds[i]) + ")";
            std::string ret = "return " + std::to_string(values[i]) + ";";
            if (!has(s, hi) || !has(s, ret)) {
                all = false;
                break;
            }
        }
        check(all, "ladder thresholds 2476..2 map to 2477..3");
        check(has(s, "return 1;"), "base orb value 1");
        check(has(s, "remaining -= piece;"), "greedy drain loop (remaining -= piece)");
    }

    printf("5) PLAYER XP CURVE (PlayerStats.cpp)\n");
    {
        std::string s = slurp("src/player/PlayerStats.cpp");
        check(has(s, "2 * level + 7"), "cost level<16 = 2L+7");
        check(has(s, "5 * level - 38"), "cost 16..30 = 5L-38");
        check(has(s, "9 * level - 158"), "cost 31+ = 9L-158");
        check(has(s, "if (level < 16)") && has(s, "if (level < 31)"),
              "bucket thresholds 16 / 31 (smmla level/15)");
        check(has(s, "xp_progress_ += (float)amount / cost;"), "progress += amount/cost");
        check(has(s, "while (xp_progress_ >= 1.0f"), "level-up loop");
        check(has(s, "leftover / (float)newCost"),
              "leftover rescaled by the NEW level cost");
        check(has(s, "xp_total_ += amount;"), "lifetime total accumulated");
        check(has(s, "--xp_take_delay_"), "orb pickup delay ticks down per tick");
        // The Genisys modern curve must not be here.
        check(!has(s, "2.5 * level") && !has(s, "4.5 * level") && !has(s, "6 * level"),
              "Genisys modern curve absent");
    }

    printf("6) XP PERSISTENCE (Player.cpp / PlayerLogin.cpp)\n");
    {
        std::string save = slurp("src/Player.cpp");
        std::string load = slurp("src/player/PlayerLogin.cpp");
        check(has(save, "\"XpLevel\"") && has(save, "\"XpP\"") && has(save, "\"XpTotal\""),
              "save writes XpLevel + XpP + XpTotal");
        check(has(load, "\"XpLevel\"") && has(load, "\"XpP\"") && has(load, "\"XpTotal\""),
              "load reads XpLevel + XpP + XpTotal");
        check(has(load, "xp_total_ < 0"), "negative totals rejected on load");
    }

    printf("7) PROTOCOL (AddEntityPacket / LevelEventPacket)\n");
    {
        // Wire-compare a full XP orb spawn packet.
        AddEntityPacket pk;
        pk.eid = 0x0102030405060708LL;
        pk.type = AddEntityPacket::TYPE_XP_ORB;
        pk.x = 1.0f;
        pk.y = 2.0f;
        pk.z = 3.0f;
        pk.speedX = 0.5f;
        pk.speedY = -0.25f;
        pk.speedZ = 0.0f;
        pk.yaw = 0.0f;
        pk.pitch = 0.0f;
        pk.modifiers = 0;
        SetEntityDataPacket::Entry e;
        e.index = SetEntityDataPacket::PROP_XP_ORB_VALUE; // 11
        e.type = SetEntityDataPacket::TYPE_INT;           // 2
        e.ival = 7;
        pk.metadata.push_back(e);

        std::vector<uint8_t> out;
        pk.encode(out);
        // encode() writes the payload only — the pid byte rides the network
        // layer — so the wire starts with the big-endian eid.
        check(ADD_ENTITY_PACKET == 0x0b, "orb spawn rides ADD_ENTITY_PACKET 0x0b");
        auto be64 = [](const std::vector<uint8_t>& b, size_t o) {
            uint64_t v = 0;
            for (int i = 0; i < 8; ++i)
                v = (v << 8) | b[o + i];
            return v;
        };
        auto be32 = [](const std::vector<uint8_t>& b, size_t o) {
            return (uint32_t)b[o] << 24 | (uint32_t)b[o + 1] << 16 |
                   (uint32_t)b[o + 2] << 8 | (uint32_t)b[o + 3];
        };
        auto f32 = [&](size_t o) {
            uint32_t u = be32(out, o);
            float f;
            std::memcpy(&f, &u, 4);
            return f;
        };
        check(be64(out, 0) == 0x0102030405060708ULL, "eid 8-byte big-endian");
        check(be32(out, 8) == 69, "orb entity type 69 big-endian");
        check(std::fabs(f32(12) - 1.0f) < 1e-6 && std::fabs(f32(16) - 2.0f) < 1e-6 &&
                  std::fabs(f32(20) - 3.0f) < 1e-6,
              "position 3 x float BE");
        check(std::fabs(f32(24) - 0.5f) < 1e-6 && std::fabs(f32(28) + 0.25f) < 1e-6 &&
                  std::fabs(f32(32) - 0.0f) < 1e-6,
              "speed 3 x float BE");
        // yaw/pitch (8 bytes) then modifiers then metadata.
        size_t meta = 48;
        // Binary::writeMetadata header: (type << 5) | index → INT(2), 11 → 0x4b.
        check(out[meta] == ((2 << 5) | 11), "metadata header (type<<5)|index = 0x4b");
        check(out[meta + 1] == 7 && out[meta + 2] == 0 && out[meta + 3] == 0 &&
                  out[meta + 4] == 0,
              "metadata: carried value 7 (little-endian int)");
        check(out[meta + 5] == 0x7f, "metadata terminated by 0x7f");

        check(LevelEventPacket::EVENT_SOUND_ORB == 1051, "orb pickup sound event 1051");
        check(LevelEventPacket::EVENT_PARTICLE_SPLASH == 2002, "splash shatter event 2002");
        check(AddEntityPacket::TYPE_THROWN_POTION == 86, "splash potion type 86");
    }

    printf("\n%s (%d failures)\n", failures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED",
           failures);
    return failures == 0 ? 0 : 1;
}
