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
// PacketPool.h — registry that maps MCPE protocol-84 packet IDs to factory
// functions. Used by Network::dispatchSingle() to instantiate the right
// DataPacket subclass from the leading ID byte of an incoming packet.
//
// Registration is static (built once at first use). To add a new packet
// type, append it to the switch in PacketPool::create() — no other change
// required.
#pragma once

#include "DataPacket.h"
#include "Info.h"
#include <memory>
#include <functional>

namespace cursestone::network::protocol {

class PacketPool {
  public:
    using Factory = std::function<std::shared_ptr<DataPacket>()>;

    // Singleton accessor.
    static PacketPool& get() {
        static PacketPool inst;
        return inst;
    }

    // Create an instance of the packet subclass matching `pid`.
    // Returns nullptr for unknown IDs.
    std::shared_ptr<DataPacket> create(uint8_t pid) const;

  private:
    PacketPool() = default;
};

} // namespace cursestone::network::protocol
