// Sanity test for the outbound packet sanitizer.
// Build (repo root): g++ -std=c++20 -O1 -w -Isrc -o /tmp/pscheck tools/packet_sanitizer_check.cpp src/network/protocol/{PacketSanitizer,MovePlayerPacket,MoveEntityPacket,SetEntityMotionPacket,AddPlayerPacket,AddEntityPacket,AddItemEntityPacket,ExplodePacket,UpdateAttributesPacket,SetEntityDataPacket,ContainerSetContentPacket,TextPacket,FullChunkDataPacket,DataPacket}.cpp src/utils/Binary.cpp
//
// The encoders' real output must pass clean, and every violation class must
// be caught. Links against the server's own packet encoder objects so the
// wire bytes under test are the exact bytes production would send.
#include "network/protocol/PacketSanitizer.h"
#include "network/protocol/MovePlayerPacket.h"
#include "network/protocol/MoveEntityPacket.h"
#include "network/protocol/SetEntityMotionPacket.h"
#include "network/protocol/AddPlayerPacket.h"
#include "network/protocol/AddEntityPacket.h"
#include "network/protocol/AddItemEntityPacket.h"
#include "network/protocol/ExplodePacket.h"
#include "network/protocol/UpdateAttributesPacket.h"
#include "network/protocol/SetEntityDataPacket.h"
#include "network/protocol/ContainerSetContentPacket.h"
#include "network/protocol/TextPacket.h"
#include "network/protocol/FullChunkDataPacket.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using namespace cursestone::network::protocol;
using cursestone::item::ItemStack;

static int failures = 0;

static void expectClean(const char* what, uint8_t pid, const std::vector<uint8_t>& body) {
    // body includes the leading pid byte, exactly like sendEncodedBody.
    SanitizeViolation v = sanitizeOutbound(pid, body.data() + 1, body.size() - 1);
    if (v.violated) {
        printf("FAIL clean case '%s': flagged field=%s problem=%s offset=%zu\n", what, v.field,
               v.problem, v.offset);
        ++failures;
    } else {
        printf("ok   clean '%s'\n", what);
    }
}

static void expectViolated(const char* what, uint8_t pid, const std::vector<uint8_t>& body) {
    SanitizeViolation v = sanitizeOutbound(pid, body.data() + 1, body.size() - 1);
    if (!v.violated) {
        printf("FAIL violation case '%s': NOT caught\n", what);
        ++failures;
    } else {
        printf("ok   caught '%s' -> field=%s problem=%s value=%s offset=%zu\n", what, v.field,
               v.problem, formatSuspectFloat(v.value, v.bits).c_str(), v.offset);
    }
}

int main() {
    const float qnan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    // --- MovePlayer: real output clean; NaN/Inf/short all caught ---
    {
        MovePlayerPacket pk;
        pk.eid = 42;
        pk.x = 10.5f;
        pk.y = 64.0f;
        pk.z = -3.25f;
        pk.yaw = 90.0f;
        pk.bodyYaw = 90.0f;
        pk.pitch = 12.5f;
        pk.mode = 0;
        pk.onGround = true;
        std::vector<uint8_t> body;
        pk.writePacket(body);
        expectClean("MovePlayer valid", MOVE_PLAYER_PACKET, body);

        pk.x = qnan;
        body.clear();
        pk.writePacket(body);
        expectViolated("MovePlayer NaN x", MOVE_PLAYER_PACKET, body);

        pk.x = 1.0f;
        pk.y = inf;
        body.clear();
        pk.writePacket(body);
        expectViolated("MovePlayer Inf y", MOVE_PLAYER_PACKET, body);

        body.pop_back(); // truncate below the 34-byte minimum
        expectViolated("MovePlayer truncated", MOVE_PLAYER_PACKET, body);
    }

    // --- MoveEntity ---
    {
        MoveEntityPacket pk;
        pk.eid = 7;
        pk.x = 1.0f;
        pk.y = 2.0f;
        pk.z = 3.0f;
        pk.yaw = 180.0f;
        pk.pitch = -10.0f;
        pk.headYaw = 180.0f;
        std::vector<uint8_t> body;
        pk.writePacket(body);
        expectClean("MoveEntity valid", MOVE_ENTITY_PACKET, body);

        pk.z = qnan;
        body.clear();
        pk.writePacket(body);
        expectViolated("MoveEntity NaN z", MOVE_ENTITY_PACKET, body);
    }

    // --- SetEntityMotion: two-record run, clean; NaN in 2nd record caught ---
    {
        SetEntityMotionPacket pk;
        typename SetEntityMotionPacket::Motion m1;
        m1.eid = 1;
        m1.motX = 0.1f;
        m1.motY = -0.3f;
        m1.motZ = 0.2f;
        typename SetEntityMotionPacket::Motion m2 = m1;
        m2.eid = 2;
        pk.entities.push_back(m1);
        pk.entities.push_back(m2);
        std::vector<uint8_t> body;
        pk.writePacket(body);
        expectClean("SetEntityMotion valid", SET_ENTITY_MOTION_PACKET, body);

        pk.entities[1].motY = inf;
        body.clear();
        pk.writePacket(body);
        expectViolated("SetEntityMotion Inf motionY", SET_ENTITY_MOTION_PACKET, body);

        body.push_back(0x11); // break the stride-20 record run
        expectViolated("SetEntityMotion truncated run", SET_ENTITY_MOTION_PACKET, body);
    }

    // --- AddPlayer: username-dependent offsets must walk correctly ---
    {
        AddPlayerPacket pk;
        pk.username = "Steve_With_A_Longer_Name";
        pk.eid = 99;
        pk.x = 100.0f;
        pk.y = 70.5f;
        pk.z = -2000.75f;
        pk.yaw = 45.0f;
        pk.pitch = 0.0f;
        pk.itemId = 278;
        pk.itemCount = 1;
        std::vector<uint8_t> body;
        pk.writePacket(body);
        expectClean("AddPlayer valid", ADD_PLAYER_PACKET, body);

        pk.speedY = qnan;
        body.clear();
        pk.writePacket(body);
        expectViolated("AddPlayer NaN speedY", ADD_PLAYER_PACKET, body);
    }

    // --- AddEntity ---
    {
        AddEntityPacket pk;
        pk.eid = 5;
        pk.type = 10;
        pk.x = 0.0f;
        pk.y = 64.0f;
        pk.z = 0.0f;
        pk.yaw = 0.0f;
        pk.pitch = 0.0f;
        std::vector<uint8_t> body;
        pk.writePacket(body);
        expectClean("AddEntity valid", ADD_ENTITY_PACKET, body);

        pk.y = inf;
        body.clear();
        pk.writePacket(body);
        expectViolated("AddEntity Inf y", ADD_ENTITY_PACKET, body);
    }

    // --- AddItemEntity: slot with NBT must be skipped correctly ---
    {
        AddItemEntityPacket pk;
        pk.eid = 12;
        pk.itemId = 264;
        pk.itemCount = 3;
        pk.itemDamage = 0;
        pk.itemNbt = std::string(40, '\xAB'); // payload bytes incl. 0x00-ish traps
        pk.x = 8.0f;
        pk.y = 64.0f;
        pk.z = 9.0f;
        std::vector<uint8_t> body;
        pk.writePacket(body);
        expectClean("AddItemEntity valid w/ NBT", ADD_ITEM_ENTITY_PACKET, body);

        pk.y = qnan;
        body.clear();
        pk.writePacket(body);
        expectViolated("AddItemEntity NaN y", ADD_ITEM_ENTITY_PACKET, body);

        AddItemEntityPacket empty;
        empty.eid = 13;
        empty.x = 1.0f;
        std::vector<uint8_t> b2;
        empty.writePacket(b2);
        expectClean("AddItemEntity empty slot", ADD_ITEM_ENTITY_PACKET, b2);
    }

    // --- Explode ---
    {
        ExplodePacket pk;
        pk.x = 1.0f;
        pk.y = 64.0f;
        pk.z = 2.0f;
        pk.radius = 3.0f;
        std::vector<uint8_t> body;
        pk.writePacket(body);
        expectClean("Explode valid", EXPLODE_PACKET, body);

        pk.radius = inf;
        body.clear();
        pk.writePacket(body);
        expectViolated("Explode Inf radius", EXPLODE_PACKET, body);
    }

    // --- UpdateAttributes: entry walk must land exactly on the name ---
    {
        UpdateAttributesPacket pk;
        pk.eid = 3;
        UpdateAttributesPacket::Entry e;
        e.min = 0.0f;
        e.max = 20.0f;
        e.value = 20.0f;
        e.name = "minecraft:health";
        pk.entries.push_back(e);
        UpdateAttributesPacket::Entry f = e;
        f.name = "minecraft:movement";
        f.value = 0.1f;
        pk.entries.push_back(f);
        std::vector<uint8_t> body;
        pk.writePacket(body);
        expectClean("UpdateAttributes valid", UPDATE_ATTRIBUTES_PACKET, body);

        pk.entries[1].value = qnan;
        body.clear();
        pk.writePacket(body);
        expectViolated("UpdateAttributes NaN value", UPDATE_ATTRIBUTES_PACKET, body);
    }

    // --- SetEntityData: metadata walk incl. LE float/string entries ---
    {
        SetEntityDataPacket pk;
        pk.eid = 4;
        SetEntityDataPacket::Entry flags;
        flags.type = SetEntityDataPacket::TYPE_BYTE;
        flags.index = SetEntityDataPacket::PROP_FLAGS;
        flags.ival = 0x02;
        pk.entries.push_back(flags);
        SetEntityDataPacket::Entry nm;
        nm.type = SetEntityDataPacket::TYPE_STRING;
        nm.index = SetEntityDataPacket::PROP_NAMETAG;
        nm.sval = "hello world";
        pk.entries.push_back(nm);
        SetEntityDataPacket::Entry air;
        air.type = SetEntityDataPacket::TYPE_FLOAT;
        air.index = 1;
        air.fval = 0.5f;
        pk.entries.push_back(air);
        std::vector<uint8_t> body;
        pk.writePacket(body);
        expectClean("SetEntityData valid", SET_ENTITY_DATA_PACKET, body);

        pk.entries[2].fval = qnan;
        body.clear();
        pk.writePacket(body);
        expectViolated("SetEntityData NaN metadata float", SET_ENTITY_DATA_PACKET, body);
    }

    // --- ContainerSetContent: slots + NBT + hotbar block ---
    {
        ContainerSetContentPacket pk;
        pk.windowid = ContainerSetContentPacket::SPECIAL_INVENTORY;
        ItemStack s;
        s.id = 1; // stone
        s.count = 64;
        s.meta = 0;
        pk.slots.push_back(s);
        ItemStack t;
        t.id = 0;
        pk.slots.push_back(t);
        ItemStack u = s;
        u.nbt = std::string(30, '\x01');
        pk.slots.push_back(u);
        for (int i = 0; i < 9; ++i)
            pk.hotbar.push_back(i);
        std::vector<uint8_t> body;
        pk.writePacket(body);
        expectClean("ContainerSetContent valid", CONTAINER_SET_CONTENT_PACKET, body);
    }

    // --- Unclassified packet families must never be flagged ---
    {
        FullChunkDataPacket pk;
        std::vector<uint8_t> body;
        pk.writePacket(body);
        expectClean("FullChunkData untouched", FULL_CHUNK_DATA_PACKET, body);

        TextPacket tp;
        tp.type = TextPacket::TYPE_RAW;
        tp.message = "hello";
        std::vector<uint8_t> b2;
        tp.writePacket(b2);
        expectClean("Text untouched", TEXT_PACKET, b2);
    }

    // --- Direct corruption of a clean ContainerSetContent count field ---
    {
        std::vector<uint8_t> body = {0x2e, 0x00, 0xff, 0xff, 0x00, 0x01}; // count=65535, 3 bytes of slots
        expectViolated("ContainerSetContent absurd count", CONTAINER_SET_CONTENT_PACKET, body);
    }

    // --- Creative catalog regression: windowid 0x79 legitimately carries
    //     ~566 slots (measured live on the real server); the absurd-count
    //     pre-filter must stay above it and the slot walk must pass. ---
    {
        std::vector<uint8_t> body;
        body.push_back(0x2e);
        body.push_back(0x79);                    // SPECIAL_CREATIVE
        body.push_back(0x02);
        body.push_back(0x36);                    // count = 566
        for (int i = 0; i < 566; ++i) {
            body.push_back(0x00);                // empty slot: 2 bytes
            body.push_back(0x00);
        }
        body.push_back(0x00);                    // trailing hotbar count = 0
        body.push_back(0x00);
        expectClean("ContainerSetContent creative catalog", CONTAINER_SET_CONTENT_PACKET, body);
    }

    if (failures == 0) {
        printf("ALL SANITIZER TESTS PASSED\n");
        return 0;
    }
    printf("%d FAILURES\n", failures);
    return 1;
}
