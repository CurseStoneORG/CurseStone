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

namespace atomikpe::network::protocol {

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

} // namespace atomikpe::network::protocol
