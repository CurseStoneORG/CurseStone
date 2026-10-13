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
// ContainerClosePacket — bidirectional: client sends it when the player
// closes a container UI (X button, Esc, moving too far away); server
// sends it to force-close a UI (e.g. chest broken while open).
//
// PMMP wire format (protocol 84) — both encode() and decode() are just
// a single windowid byte (ContainerClosePacket.php, attached as design
// reference).
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class ContainerClosePacket : public DataPacket {
public:
    uint8_t pid() const override { return CONTAINER_CLOSE_PACKET; }

    uint8_t windowid = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
