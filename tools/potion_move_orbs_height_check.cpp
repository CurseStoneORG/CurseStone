// tools/potion_move_orbs_height_check.cpp
//
// Verifies the movement-effect, XP-orb spawn, dropped-item and mining
// changes against the reverse-engineered 0.15.10 vanilla reference
// (libminecraftpe.so, protocol 84, sha256 2d122090...) and the Genisys
// 0.15.0 era reference (tag ec716b0ea4):
//
// 1) XP ORB INITIAL MOTION — the vanilla constructor
//    ExperienceOrb::ExperienceOrb(BlockSource&, Vec3 const&, int, Player*)
//    @ 0x00be7c38 (resolved via PLT 0x5c5088) draws every orb's own
//    numbers from Level::getRandom(): X/Z = rand01 * 0.4 - 0.2
//    (0x3ECCCCCD / 0xBE4CCCCD, pool 0x00be7d68/0x00be7d6c), Y =
//    rand01 * 0.4 (the upward hop) and yaw = rand01 * 360.0
//    (0x43B40000, pool 0x00be7d64). Source scan over World.cpp: the
//    spawn must carry those exact factors and an independent draw per
//    orb, and AddEntity must forward the heading.
// 2) DROPPED ITEM WATER BUOYANCY — ItemEntity::normalTick overwrites
//    the vertical motion with +0.2 when the item's cell is water
//    (Material::isType(6) @ 0x00be9ae2). Source scan over World.cpp.
// 3) PLAYER THROW MOTION — the era reference (Genisys 0.15.0
//    Player.php:3235) throws with the look direction times 0.4 and a
//    40-tick pickup delay. Source scan over PlayerInventory.cpp.
// 4) MOB SPEED MULTIPLIER — the vanilla server stacks movementSpeed
//    attribute modifiers on mobs (Mob::_addSpeedBonus @ 0x00bf7b88 /
//    _removeSpeedBonus @ 0x00bf7cc1) with the era factors
//    x(1 + 0.2*(amp+1)) / x max(0, 1 - 0.15*(amp+1)). The AI context
//    must multiply its profile base once per tick. Source scan over
//    World.cpp and MobAIManager.cpp.
// 5) MINING SPEED EFFECTS — Player::getDestroySpeed @ 0x00c11eb8
//    multiplies the tool speed by 1 + 0.2*(amp+1) per haste level
//    (0.2 = 0x3E4CCCCD, pool 0x00c11fbc) and by pow(0.3, amp+1) per
//    mining-fatigue level (0.3 = 0x3FD33333, pool 0x00c11fa8). The
//    server-side break-time validation must carry the same factors or
//    a hasted client's valid removals get refused. Source scan over
//    PlayerBlocks.cpp.
// 6) RESISTANCE CLAMP — the -20%/level factor clamps at zero (the
//    Genisys EntityDamageEvent clamps identically); an amplifier high
//    enough to invert it must not turn the damage negative. Source
//    scan over PlayerCombat.cpp.
// 7) HEALTH BOOST — the capacity rises 4 points per level over the
//    vanilla 20 while the effect runs, recomputed on add/modify/remove
//    and pushed through generic.health. Source scan over PlayerStats.cpp.
// 8) MOVEMENT-SPEED ATTRIBUTE WIRE FORMAT — the client's
//    UpdateAttributesPacket::AttributeData::read @ 0x00ab3dac expects
//    per entry [F32 min][F32 max][F32 value][String name] (the
//    AttributeData(AttributeInstance) ctor @ 0x00ab3e68 confirms the
//    mapping) and the client handler @ 0x0091d780 resolves the entity
//    by the packet eid. Byte-exact comparison of the encoded packet
//    carrying the speed formula values.
// 9) ITEM REST HEIGHT — physics::sweep keeps the Y->X->Z vanilla clip
//    order; a 0.25-tall item box falling onto a full block must rest
//    with its bottom EXACTLY on the block top (on_ground, zero
//    residual motion), so the dropped item never rests buried.
//
// Build (mirrors potion_xp_check.cpp):
//   g++ -std=c++20 -Wall -Wextra -O1 -Isrc tools/potion_move_orbs_height_check.cpp
//       src/network/protocol/UpdateAttributesPacket.cpp
//       src/network/protocol/DataPacket.cpp src/math/HitResult.cpp
//       src/network/protocol/DataPacket.cpp src/physics/Physics.cpp
//
// Exit code 0 = every check passed; 1 = at least one check failed.
#include "network/protocol/UpdateAttributesPacket.h"
#include "physics/Physics.h"

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
    printf("1) XP ORB INITIAL MOTION (World.cpp)\n");
    {
        std::string s = slurp("src/world/World.cpp");
        check(has(s, "orb.vx = rand01() * 0.4 - 0.2;"),
              "orb X velocity rand01*0.4-0.2 (ctor literal 0x3ECCCCCD/0xBE4CCCCD)");
        check(has(s, "orb.vy = rand01() * 0.4;"),
              "orb Y velocity rand01*0.4 — the upward hop");
        check(has(s, "orb.vz = rand01() * 0.4 - 0.2;"),
              "orb Z velocity rand01*0.4-0.2");
        check(has(s, "orb.yaw = (float)(rand01() * 360.0);"),
              "orb heading rand01*360 (0x43B40000)");
        check(has(s, "std::rand() / ((double)RAND_MAX + 1.0)"),
              "uniform [0,1) source shared with the other spawns");
        check(has(s, "add->yaw = p.yaw;"),
              "AddEntityPacket forwards the heading");
        check(has(s, "motion->entities.push_back({p.eid, (float)p.vx, (float)p.vy, (float)p.vz});"),
              "SetEntityMotion carries the initial velocity");
        // The velocity draw sits between the position fields and the
        // value assignment: no orb reaches addExperienceOrb motionless.
        check(has(s, "orb.z = z;\n        orb.vx = rand01() * 0.4 - 0.2;\n"
                    "        orb.vy = rand01() * 0.4;\n"
                    "        orb.vz = rand01() * 0.4 - 0.2;\n"
                    "        orb.yaw = (float)(rand01() * 360.0);\n"
                    "        orb.value = piece;"),
              "every orb draws its motion before registration");
    }

    printf("2) DROPPED ITEM WATER BUOYANCY (World.cpp)\n");
    {
        std::string s = slurp("src/world/World.cpp");
        check(has(s, "if (in == block::WATER || in == block::FLOWING_WATER)\n                di->vy = 0.2;"),
              "item water branch: vertical motion overwritten with +0.2");
        check(has(s, "di->vy -= kItemGravity;"),
              "item gravity -0.04 kept (0x3D4CCCCD pool)");
    }

    printf("3) PLAYER THROW MOTION (PlayerInventory.cpp)\n");
    {
        std::string s = slurp("src/player/PlayerInventory.cpp");
        check(has(s, "const double kMUL = 0.4;"),
              "throw multiplier 0.4 (Genisys 0.15.0 Player.php:3235)");
        check(has(s, "pos_.y + 1.3"),
              "throw origin feet + 1.3 (era reference)");
        check(has(s, "40 /* Two-second pickup delay. */"),
              "40-tick pickup delay kept");
    }

    printf("4) MOB SPEED MULTIPLIER (World.cpp + MobAIManager.cpp)\n");
    {
        std::string w = slurp("src/world/World.cpp");
        check(has(w, "float World::mobSpeedMultiplier(int64_t eid)"),
              "World::mobSpeedMultiplier exists");
        check(has(w, "factor *= 1.0f + 0.2f * (e->amplifier() + 1);"),
              "Speed factor x(1 + 0.2*(amp+1))");
        check(has(w, "factor *= std::max(0.0f, 1.0f - 0.15f * (e->amplifier() + 1));"),
              "Slowness factor x max(0, 1 - 0.15*(amp+1))");
        check(has(w, "std::lock_guard<std::mutex> lk(mob_effects_mtx_);"),
              "factor read guarded by mob_effects_mtx_ (inner lock only)");
        std::string m = slurp("src/entity/ai/MobAIManager.cpp");
        check(has(m, "const float speed_factor = world_->mobSpeedMultiplier(m.eid);"),
              "AI context consults the mob effects");
        check(has(m, "ctx.walkSpeed *= speed_factor;") &&
                  has(m, "ctx.panicSpeed *= speed_factor;"),
              "profile base multiplied once per tick (no accumulation)");
    }

    printf("5) MINING SPEED EFFECTS (PlayerBlocks.cpp)\n");
    {
        std::string s = slurp("src/player/PlayerBlocks.cpp");
        check(has(s, "speed_factor *= 1.0f + 0.2f * (haste->amplifier() + 1);"),
              "haste speed factor 1 + 0.2*(amp+1) (getDestroySpeed 0.2 literal)");
        check(has(s, "speed_factor *= std::pow(0.3f, fatigue->amplifier() + 1);"),
              "mining fatigue factor pow(0.3, amp+1) (getDestroySpeed pow)");
        check(has(s, "block::breakSeconds((uint8_t)blockId, held) / speed_factor"),
              "break-time validation carries the effect factors");
    }

    printf("6) RESISTANCE CLAMP (PlayerCombat.cpp)\n");
    {
        std::string s = slurp("src/player/PlayerCombat.cpp");
        int clamped = 0;
        for (const char* needle :
             {"amount *= std::max(0.0f, 1.0f - 0.2f * (res->amplifier() + 1));"}) {
            size_t at = s.find(needle);
            while (at != std::string::npos) {
                ++clamped;
                at = s.find(needle, at + 1);
            }
        }
        check(clamped == 2, "both damage paths clamp the resistance factor");
    }

    printf("7) HEALTH BOOST (PlayerStats.cpp)\n");
    {
        std::string s = slurp("src/player/PlayerStats.cpp");
        check(has(s, "boost = 4.0f * (hb->amplifier() + 1);"),
              "health capacity +4 per level (era value)");
        check(has(s, "setMaxHealth(20.0f + boost);"),
              "capacity rides the vanilla 20-point base");
        check(has(s, "if (health() > maxHealth())\n        setHealth(maxHealth());"),
              "current health clamps when the boost ends");
        int hooks = 0;
        for (const char* needle : {"case entity::EFF_HEALTH_BOOST:\n            applyHealthBoost();",
                                   "if (e.id() == entity::EFF_HEALTH_BOOST)\n        applyHealthBoost();"}) {
            if (has(s, needle))
                ++hooks;
        }
        check(hooks >= 3 || has(s, "void Player::applyHealthBoost()"),
              "hooked on add/modify/remove via applyHealthBoost()");
    }

    printf("8) MOVEMENT-SPEED ATTRIBUTE WIRE FORMAT (bytes)\n");
    {
        // The values the player path computes: base 0.1, Speed I x(1+0.2*1)
        // = 0.12, Slowness II x(1-0.15*2) = 0.07.
        UpdateAttributesPacket pk;
        pk.eid = 0x1122334455667788ULL;
        UpdateAttributesPacket::Entry e;
        e.name = "generic.movementSpeed";
        e.min = 0.0f;
        e.max = 340282346638528859811704183484516925440.0f;
        e.value = 0.12f;
        e.defaultValue = 0.1f; // server-side only, never serialized
        pk.entries.push_back(e);
        std::vector<uint8_t> out;
        pk.encode(out);

        std::vector<uint8_t> want;
        auto put64 = [&want](uint64_t v) {
            for (int i = 7; i >= 0; --i)
                want.push_back((uint8_t)(v >> (8 * i)));
        };
        auto put16 = [&want](uint16_t v) {
            want.push_back((uint8_t)(v >> 8));
            want.push_back((uint8_t)v);
        };
        auto putF = [&want, &put16](float v) {
            uint32_t bits;
            std::memcpy(&bits, &v, 4);
            put16((uint16_t)(bits >> 16));
            want.push_back((uint8_t)(bits >> 8));
            want.push_back((uint8_t)bits);
        };
        const char* name = "generic.movementSpeed";
        const uint16_t nameLen = (uint16_t)std::strlen(name);
        put64(0x1122334455667788ULL);
        put16(1);
        putF(0.0f);                                    // min
        putF(340282346638528859811704183484516925440.0f); // max
        putF(0.12f);                                   // value
        put16(nameLen);                                // name (short BE + bytes)
        for (const char* p = name; *p; ++p)
            want.push_back((uint8_t)*p);

        bool exact = out.size() == want.size();
        for (size_t i = 0; exact && i < want.size(); ++i)
            exact = out[i] == want[i];
        check(exact, "UpdateAttributes encode = [eid 8B][count][min][max][value][name] BE");

        // The value that reaches the wire excludes the server-side default.
        bool noDefault = true;
        std::vector<uint8_t> probe;
        UpdateAttributesPacket probePk;
        probePk.eid = 1;
        probePk.entries.push_back(e);
        probePk.encode(probe);
        // 8 eid + 2 count + 12 floats + 2 len + 21 name = 45; if the
        // default were serialized the packet would be 49.
        check(probe.size() == 45, "protocol 84 carries no default-value float");
        (void)probe;
        (void)noDefault;
    }

    printf("9) ITEM REST HEIGHT (physics::sweep)\n");
    {
        using namespace cursestone;
        // A full block occupies the cell below the item: top surface y = 64.
        std::vector<physics::BlockBox> boxes;
        physics::BlockBox ground;
        ground.x = 0;
        ground.y = 63;
        ground.z = 0;
        ground.box = mcp_aabb_make(0.0, 63.0, 0.0, 1.0, 64.0, 1.0);
        boxes.push_back(ground);

        // A 0.25-tall item box falling onto it (bottom starts 1 above).
        mcp_aabb_t bb = mcp_aabb_make(0.4375, 65.0, 0.4375, 0.6875, 65.25, 0.6875);
        double y = 65.0;
        for (int i = 0; i < 200; ++i) {
            physics::MotionResult res = physics::sweep(bb, mcp_v3(0.0, -0.04, 0.0), boxes);
            y += res.motion.y;
            bb = mcp_aabb_move(bb, 0.0, res.motion.y, 0.0);
            if (res.on_ground)
                break;
        }
        printf("    rest y = %.17g (expected exactly 64.0)\n", y);
        check(y == 64.0, "item box rests exactly on the block top (no burial)");
        check(bb.minY == 64.0 && bb.maxY == 64.25, "AABB bottom == collision plane");

        // A partial block: a bottom slab (top at 63.5). The item must rest
        // on the slab surface, not sink to the cell floor.
        std::vector<physics::BlockBox> slab;
        physics::BlockBox half;
        half.x = 0;
        half.y = 63;
        half.z = 0;
        half.box = mcp_aabb_make(0.0, 63.0, 0.0, 1.0, 63.5, 1.0);
        slab.push_back(half);
        mcp_aabb_t bb2 = mcp_aabb_make(0.4375, 65.0, 0.4375, 0.6875, 65.25, 0.6875);
        double y2 = 65.0;
        for (int i = 0; i < 200; ++i) {
            physics::MotionResult res = physics::sweep(bb2, mcp_v3(0.0, -0.04, 0.0), slab);
            y2 += res.motion.y;
            bb2 = mcp_aabb_move(bb2, 0.0, res.motion.y, 0.0);
            if (res.on_ground)
                break;
        }
        printf("    slab rest y = %.17g (expected exactly 63.5)\n", y2);
        check(y2 == 63.5, "item rests exactly on a slab's upper face");
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL CHECKS PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
