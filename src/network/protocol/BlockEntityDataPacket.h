/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// BlockEntityDataPacket — cliente→servidor cuando el jugador edita un
// sign (when leaving the text screen). The server also uses it
// para re-sincronizar el tile.
//
// Wire format (protocolo 84, mirrors Genisys BlockEntityDataPacket):
//   [Int BE] x
//   [Int BE] y
//   [Int BE] z
//   [rest]   namedtag      // little-endian NBT tree of the block entity
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class BlockEntityDataPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return BLOCK_ENTITY_DATA_PACKET;
    }

    int32_t x = 0, y = 0, z = 0;
    std::vector<uint8_t> namedtag;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
