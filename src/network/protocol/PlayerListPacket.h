// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerListPacket.h — sync the server-side player list (tab list) to client.
//
// Wire format (protocol 84) — PMMP PlayerListPacket::encode:
//   [1 byte ID 0x38]
//   [Uint8]   type     (0=ADD, 1=REMOVE)
//   [Int32 BE] count
//   for each entry:
//     if (type == ADD):
//       [16 bytes] uuid
//       [Int64 BE] eid
//       [BE String] name
//       [BE String] isSlim ("false"/"true")
//       [BE String] skin
//     else:
//       [16 bytes] uuid
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>
#include <array>
#include <string>

namespace cenisyspe::network::protocol {

class PlayerListPacket : public DataPacket {
public:
    uint8_t pid() const override { return PLAYER_LIST_PACKET; }

    static constexpr uint8_t TYPE_ADD    = 0;
    static constexpr uint8_t TYPE_REMOVE = 1;

    struct Entry {
        std::array<uint8_t, 16> uuid{};
        int64_t     eid   = 0;
        std::string name;
        std::string isSlim;
        std::string skin;
    };

    uint8_t type = TYPE_ADD;
    std::vector<Entry> entries;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cenisyspe::network::protocol
