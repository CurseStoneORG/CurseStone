// SPDX-License-Identifier: LGPL-3.0-or-later
// SetEntityDataPacket.h — server→client entity metadata (pose flags).
//
// Wire format (protocol 84) — Genisys 0.15.0
// SetEntityDataPacket::encode + Binary::writeMetadata():
//
//     $this->putLong($this->eid);
//     $meta = Binary::writeMetadata($this->metadata);
//     $this->put($meta);
//
// and writeMetadata() serialises each entry as
//
//     chr(($type << 5) | ($index & 0x1F))   // key byte: type in top 3 bits
//     <value bytes, per $type>
//     ...
//     "\x7f"                                 // terminator
//
// This packet is how the SERVER tells other clients about a pose change.
// Genisys 0.15 itself never sends it for sneaking (its Player::setSneaking()
// is local-only — see Player.php:3017-3034, which only flips the local
// property), which is exactly why this server's players were invisible to
// each other while crouching. The reference here is Genisys' *metadata
// encoding* plus the vanilla semantic of Entity::DATA_FLAGS.
//
// The pose bits we care about (Genisys Entity.php:87-105):
//     DATA_FLAGS    = 0   (byte)   -- bit 1 = SNEAKING, bit 3 = SPRINTING
//
// They all live in the SAME property, so every update has to send the full
// byte (sneak bit + sprint bit), never just one of the bits — otherwise we
// would clobber the other bit on the receiving client.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>
#include <string>

namespace cursestone::network::protocol {

class SetEntityDataPacket : public DataPacket {
public:
    uint8_t pid() const override { return SET_ENTITY_DATA_PACKET; }

    // Value types (Genisys Entity::DATA_TYPE_*).
    static constexpr uint8_t TYPE_BYTE   = 0;
    static constexpr uint8_t TYPE_SHORT  = 1;
    static constexpr uint8_t TYPE_INT    = 2;
    static constexpr uint8_t TYPE_FLOAT  = 3;
    static constexpr uint8_t TYPE_STRING = 4;
    static constexpr uint8_t TYPE_POS    = 6;
    static constexpr uint8_t TYPE_LONG   = 7;

    // Metadata terminator byte (Genisys Binary::writeMetadata).
    static constexpr uint8_t TERMINATOR = 0x7F;

    // Well-known property indices / flag bits (Genisys Entity.php).
    static constexpr uint8_t PROP_FLAGS           = 0;   // byte
    static constexpr uint8_t FLAG_BIT_ONFIRE      = 0;
    static constexpr uint8_t FLAG_BIT_SNEAKING    = 1;
    static constexpr uint8_t FLAG_BIT_RIDING      = 2;
    static constexpr uint8_t FLAG_BIT_SPRINTING   = 3;
    static constexpr uint8_t FLAG_BIT_ACTION      = 4;
    static constexpr uint8_t FLAG_BIT_INVISIBLE   = 5;

    struct Entry {
        uint8_t index = 0;
        uint8_t type  = TYPE_BYTE;
        int64_t ival  = 0;      // BYTE / SHORT / INT / LONG
        float   fval  = 0.0f;   // FLOAT
        std::string sval;       // STRING
    };

    int64_t eid = 0;
    std::vector<Entry> entries;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol