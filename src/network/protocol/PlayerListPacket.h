// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerListPacket.h — sync the server-side player list (tab list) to client.
//
// Wire format (protocol 84) — Genisys 0.15.0 PlayerListPacket::encode
// (the tag the reference implementation for this protocol ships):
//   [1 byte ID 0x38]
//   [Uint8]   type     (0=ADD, 1=REMOVE)
//   [Int32 BE] count
//   for each entry:
//     if (type == ADD):
//       [16 bytes] uuid
//       [Int64 BE] eid
//       [BE String] name
//       [BE String] skinId      <- NOT "isSlim"
//       [BE String] skinData    <- the player's skin blob, never empty
//       (a "BE String" here is putString: uint16 BE length + bytes —
//        Genisys 0.15.0 BinaryStream::putString — NOT a 4-byte LString,
//        which is reserved for the login JWT)
//     else:
//       [16 bytes] uuid
//
// The class comment in the reference packet file says "entity id, name,
// isSlim, skin", but the code that actually writes it — and every server
// built on it — puts the skin ID and the skin data there:
//     Server::updatePlayerListData() builds
//       [$uuid, $entityId, $name, $skinId, $skinData]
// and Human::spawnTo() refuses to spawn an entity whose skin is shorter
// than 64*32*4 bytes. Sending "false" + "" here makes the 0.15.10 client
// try to load a skin from an empty blob and DIE on it, which is exactly
// what happens the moment a second player joins: both clients crash
// within ~2 seconds of the PlayerListPacket/AddPlayerPacket pair.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>
#include <array>
#include <string>

namespace cursestone::network::protocol {

class PlayerListPacket : public DataPacket {
public:
    uint8_t pid() const override { return PLAYER_LIST_PACKET; }

    static constexpr uint8_t TYPE_ADD    = 0;
    static constexpr uint8_t TYPE_REMOVE = 1;

    struct Entry {
        std::array<uint8_t, 16> uuid{};
        int64_t     eid   = 0;
        std::string name;
        std::string skinId;      // the player's own SkinId from its login
        std::string skinData;    // its decoded SkinData blob (never empty)
    };

    uint8_t type = TYPE_ADD;
    std::vector<Entry> entries;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
