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

/// @brief Dimension change (protocol 84).
///
/// Sent right before moving a player into a world of another dimension
/// (0 Overworld, 1 Nether). The client shows its dimension loading
/// screen and leaves it once the new chunks and the final position
/// arrive. Body layout (same bytes PMMP/Genisys send on protocol 84):
///
///   [Uint8   ] dimension
///   [Float BE] x
///   [Float BE] y
///   [Float BE] z
///   [Uint8   ] unknown (0)
class ChangeDimensionPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return CHANGE_DIMENSION_PACKET;
    }

    int32_t dimension = 0; ///< 0 Overworld, 1 Nether
    float x = 0.0f;        ///< destination X
    float y = 0.0f;        ///< destination Y
    float z = 0.0f;        ///< destination Z
    uint8_t unknown = 0;   ///< ignored by the 0.15.x client

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
