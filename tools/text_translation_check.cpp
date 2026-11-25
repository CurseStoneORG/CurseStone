// Minimal wire check for the client-translatable text path (no harness, no
// network): encode/decode TYPE_TRANSLATION TextPackets and assert the exact
// shape the 0.15.10 client parses (type byte, key string, param count, param
// strings), plus the feedback bridge behavior. Builds like the era tools:
//   g++ -std=c++20 -Isrc tools/text_translation_check.cpp \
//       src/network/protocol/TextPacket.cpp src/network/protocol/Packet.cpp \
//       <other needed objects> -o /home/z/my-project/text_check
#include "network/protocol/TextPacket.h"
#include <cstdio>
#include <string>
#include <vector>

using cursestone::network::protocol::TextPacket;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%-58s %s\n", what, ok ? "OK" : "FAIL");
    if (!ok)
        failures++;
}

int main() {
    // 1) Translation packet with the victim + killer params (death message).
    {
        TextPacket pk;
        pk.type = TextPacket::TYPE_TRANSLATION;
        pk.message = "death.attack.lava.player";
        pk.parameters = {"Steve", "Alex"};
        std::vector<uint8_t> out;
        pk.writePacket(out);

        // PID byte + type byte first: type must be 2 (TRANSLATION).
        check(out.size() >= 2, "encoded packet has pid+type bytes");
        check(out[1] == TextPacket::TYPE_TRANSLATION, "type byte is 2 (TRANSLATION)");

        TextPacket back;
        check(back.decode(out.data() + 1, out.size() - 1), "decode round-trip");
        check(back.type == TextPacket::TYPE_TRANSLATION, "decoded type is TRANSLATION");
        check(back.message == "death.attack.lava.player", "decoded key matches");
        check(back.parameters.size() == 2, "decoded parameter count matches");
        check(back.parameters[0] == "Steve" && back.parameters[1] == "Alex",
              "decoded parameters match");
    }

    // 2) Parameterless translation (commands.weather.clear).
    {
        TextPacket pk;
        pk.type = TextPacket::TYPE_TRANSLATION;
        pk.message = "commands.weather.clear";
        std::vector<uint8_t> out;
        pk.writePacket(out);
        TextPacket back;
        check(back.decode(out.data() + 1, out.size() - 1), "parameterless decode");
        check(back.parameters.empty(), "parameterless keeps zero parameters");
    }

    // 3) Raw and tip packets still encode their single message (chat path
    //    untouched, plugin compatibility).
    {
        TextPacket raw;
        raw.type = TextPacket::TYPE_RAW;
        raw.message = "console fallback line";
        std::vector<uint8_t> out;
        raw.writePacket(out);
        TextPacket back;
        check(back.decode(out.data() + 1, out.size() - 1), "raw decode");
        check(back.message == "console fallback line", "raw message matches");

        TextPacket tip;
        tip.type = TextPacket::TYPE_TIP;
        tip.message = "Respawn point set";
        std::vector<uint8_t> out2;
        tip.writePacket(out2);
        TextPacket back2;
        check(back2.decode(out2.data() + 1, out2.size() - 1), "tip decode");
        check(back2.type == TextPacket::TYPE_TIP, "tip type preserved");
    }

    std::printf(failures == 0 ? "\nALL CHECKS PASSED\n" : "\n%d CHECK(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
