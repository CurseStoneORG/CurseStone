/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
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
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

/// @brief change of dimension (protocolo 84).
///
/// the server it sends justo before move to a player to a world
/// of other dimension (0 surface, 1 Nether). the client shows its
/// screen of load of dimension and leaves of ella when receives the
/// chunks new and the position end. format of the cuerpo:
///
///   [Int32 BE] dimension
///   [Float BE] x
///   [Float BE] and
///   [Float BE] z
///   [Uint8   ] unknown (0)
class ChangeDimensionPacket : public DataPacket {
public:
    uint8_t pid() const override { return CHANGE_DIMENSION_PACKET; }

    int32_t dimension = 0;  ///< 0 surface, 1 Nether
    float x = 0.0f;         ///< X of target
    float y = 0.0f;         ///< and of target
    float z = 0.0f;         ///< Z of target
    uint8_t unknown = 0;    ///< the client of 0.15.x it ignora

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
