// tools/potion_effect_check.cpp
//
// Verifies the potion effect layer against the reverse-engineered 0.15.10
// vanilla reference (libminecraftpe.so, protocol 84, sha256 2d122090...) and
// the Genisys 0.15.0 era reference (tag ec716b0ea4):
//
// 1) PACKET ID — MobEffectPacket = 0x19, measured in the vanilla
//    MobEffectPacket::getId() Thumb stub (movs r0,#0x19; bx lr) at
//    0x00aaad34; Genisys Info.php declares the same constant.
// 2) ADD/MODIFY WIRE FORMAT — vanilla MobEffectPacket::read (0x00aaae1c)
//    parses [eid 8B BE][eventId][effectId][amplifier][particles][duration
//    4B BE]; the server writes the identical layout (ServerPlayer::
//    onEffectAdded 0x00b2c904: duration at +0x18, eventId at +0x1c,
//    effectId +0x1d, amplifier +0x1e, particles +0x1f). Byte-exact
//    comparison against the CurseStone encoder.
// 3) REMOVE ZEROING — the vanilla server strips a removal down to the
//    identifying fields: amplifier=0, particles=0, duration=0
//    (ServerPlayer::onEffectRemoved 0x00b2ca08). Byte-exact comparison.
// 4) REPLACEMENT RULE — Living::addEffect must implement the vanilla
//    MobEffectInstance::update semantics (0x00ba5bf0): higher amplifier
//    takes both amplifier and duration; equal amplifier only takes a
//    strictly longer duration; weaker amplifier keeps the running effect.
//    Source scan over Living.cpp.
// 5) SPLASH GEOMETRY — the splash volume is the hit point grown by
//    (4, 2, 4) with a per-entity dist^2 < 16 cut, base duration x0.75 and
//    per-entity (1 - dist/4) + 0.5 scaling (ThrownPotion::onHit
//    0x00c208b4, SPLASH_RANGE 0x01547a88 = 4.0f). Source scan over
//    World.cpp.
// 6) INTERVAL TICKS — the periodic effect intervals of the era reference
//    (Genisys 0.15.0 Effect::canTick): poison 25>>amp, wither 50>>amp,
//    regeneration 40>>amp, hunger 40>>amp, saturation 20>>amp. Unit test
//    against Effect.cpp.
// 7) HUNGER DRAIN — one food point per occurrence (Genisys
//    Effect::applyEffect HUNGER), no exhaustion. Source scan over
//    PlayerStats.cpp.
//
//   g++ -std=c++20 -Wall -Wextra -O1 -Isrc tools/potion_effect_check.cpp \
//       src/network/protocol/MobEffectPacket.cpp src/network/protocol/DataPacket.cpp \
//       src/entity/Effect.cpp -o /tmp/tools/potion_effect_check
//
// Exit code 0 = every check passed; 1 = at least one check failed.
#include "network/protocol/MobEffectPacket.h"
#include "entity/Effect.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace cursestone::network::protocol;
using cursestone::entity::Effect;

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
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}
static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

int main() {
    printf("potion_effect_check\n");

    // ---------- 1) packet id ----------
    {
        printf("[1] packet id\n");
        MobEffectPacket pk;
        check(pk.pid() == 0x19, "MobEffectPacket id is 0x19");
    }

    // ---------- 2) ADD / MODIFY wire ----------
    {
        printf("[2] add/modify wire (vanilla read 0x00aaae1c layout)\n");
        Effect e = Effect::create(14, 3600, 2); // Invisibility II, 3 minutes
        MobEffectPacket add;
        add.eid = 0x0123456789ABCDEFULL;
        add.eventId = MobEffectPacket::EVENT_ADD;
        add.effectId = (uint8_t)e.id();
        add.amplifier = (uint8_t)e.amplifier();
        add.particles = true;
        add.duration = e.duration();
        std::vector<uint8_t> out;
        add.encode(out);
        // 8 (eid) + 4 bytes + 4 (duration)
        const uint8_t expected_add[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD,
                                        0xEF, 0x01, 0x0E, 0x02, 0x01, 0x00, 0x00,
                                        0x0E, 0x10};
        check(out.size() == 16, "ADD payload is 16 bytes");
        check(std::memcmp(out.data(), expected_add, 16) == 0,
              ("ADD bytes match the vanilla layout: " + hex(out)).c_str());

        MobEffectPacket modify;
        modify.eid = 1;
        modify.eventId = MobEffectPacket::EVENT_MODIFY;
        modify.effectId = 1;
        modify.amplifier = 0;
        modify.particles = true;
        modify.duration = 25;
        out.clear();
        modify.encode(out);
        const uint8_t expected_mod[] = {0, 0, 0, 0, 0, 0, 0, 1, 0x02, 0x01, 0x00,
                                        0x01, 0, 0, 0, 25};
        check(std::memcmp(out.data(), expected_mod, 16) == 0,
              ("MODIFY bytes match: " + hex(out)).c_str());
    }

    // ---------- 3) REMOVE zeroing ----------
    {
        printf("[3] remove zeroing (ServerPlayer::onEffectRemoved 0x00b2ca08)\n");
        // The zeroing lives in the production path (Player::makeEffectPacket);
        // verify both the source and the bytes a zeroed REMOVE produces.
        std::string stats = slurp("src/player/PlayerStats.cpp");
        check(contains(stats, "if (eventId == network::protocol::MobEffectPacket::EVENT_REMOVE)"),
              "makeEffectPacket special-cases REMOVE");
        check(contains(stats, "pk->amplifier = 0;\n        pk->particles = false;\n        pk->duration = 0;"),
              "REMOVE strips amplifier/particles/duration to zero");
        MobEffectPacket rm;
        rm.eid = 42;
        rm.eventId = MobEffectPacket::EVENT_REMOVE;
        rm.effectId = 19; // Poison
        rm.amplifier = 0;
        rm.particles = false;
        rm.duration = 0;
        std::vector<uint8_t> out;
        rm.encode(out);
        const uint8_t expected_rm[] = {0, 0, 0, 0, 0, 0, 0, 42, 0x03, 0x13, 0x00,
                                       0x00, 0, 0, 0, 0};
        check(out.size() == 16 && std::memcmp(out.data(), expected_rm, 16) == 0,
              ("REMOVE bytes match the vanilla wire: " + hex(out)).c_str());
    }

    // ---------- 4) vanilla replacement rule ----------
    {
        printf("[4] replacement rule (MobEffectInstance::update 0x00ba5bf0)\n");
        std::string living = slurp("src/entity/Living.cpp");
        check(contains(living, "if (e->amplifier() < existing->amplifier())"),
              "weaker amplifier keeps the running effect");
        check(contains(living,
                       "e->amplifier() == existing->amplifier() &&\n"
                       "            e->duration() <= existing->duration())"),
              "equal amplifier only takes a strictly longer duration");
        check(!contains(living, "bool weaker ="),
              "the old weaker-ignores-equal-duration rule is gone");
    }

    // ---------- 5) splash geometry ----------
    {
        printf("[5] splash geometry (ThrownPotion::onHit 0x00c208b4)\n");
        std::string world = slurp("src/world/World.cpp");
        check(contains(world, "px - 4.0 && ex <= px + 4.0"), "volume grows 4 horizontally");
        check(contains(world, "ey >= py - 2.0 && ey <= py + 2.0"), "volume grows 2 vertically");
        check(contains(world, "d2 >= 16.0"), "per-entity dist^2 < 16 cut");
        check(contains(world, "0.75f"), "splash base is 3/4 of the drinkable duration");
        check(contains(world, "0.25f) * base + 0.5f"),
              "per-entity (1 - dist/4) scaling with +0.5 rounding");
        check(contains(world, "addMobEffect(meid"),
              "mobs in the volume receive the effect");
        check(contains(world, "dowseFireAround"), "water splash dowse exists");
        check(contains(world, "p->meta == 0"), "only plain water dowse (vanilla route)");
    }

    // ---------- 6) interval ticks ----------
    {
        printf("[6] interval ticks (Genisys 0.15.0 Effect::canTick)\n");
        // Poison I: 25-tick interval — fires at durations 25, 50, 75...
        Effect poison = Effect::create(19, 50, 0);
        bool poison_ok = true;
        for (int d = 1; d <= 50; ++d) {
            Effect probe = Effect::create(19, d, 0);
            bool fires = (d % 25) == 0;
            if (probe.canTick() != fires)
                poison_ok = false;
        }
        check(poison_ok && poison.canTick(), "poison fires every 25 ticks at amp 0");
        // Wither I: 50 ticks; Regen I: 40; Hunger I: 40; Saturation I: 20.
        auto firesAt = [](uint8_t id, int amp, int mod) {
            Effect probe = Effect::create(id, mod, amp);
            return probe.canTick();
        };
        check(firesAt(20, 0, 50) && !firesAt(20, 0, 51), "wither interval 50>>amp");
        check(firesAt(10, 0, 40) && !firesAt(10, 0, 41), "regeneration interval 40>>amp");
        check(firesAt(17, 0, 40) && !firesAt(17, 0, 41), "hunger interval 40>>amp");
        check(firesAt(23, 0, 20) && !firesAt(23, 0, 21), "saturation interval 20>>amp");
        check(firesAt(19, 1, 12) && !firesAt(19, 1, 13), "poison II interval 12 (25>>1)");
    }

    // ---------- 7) hunger drain ----------
    {
        printf("[7] hunger drain (Genisys Effect::applyEffect HUNGER)\n");
        std::string stats = slurp("src/player/PlayerStats.cpp");
        check(contains(stats, "HungerChangeReason::HUNGER_EFFECT"),
              "hunger drains food directly with the dedicated reason");
        check(!contains(stats, "addExhaustion(0.1f * (amplifier + 1), event::player::PlayerExhaustEvent::CAUSE_POTION)"),
              "the old exhaustion drain is gone");
    }

    printf(failures == 0 ? "ALL PASS\n" : "FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
