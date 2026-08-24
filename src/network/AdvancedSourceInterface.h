// SPDX-License-Identifier: LGPL-3.0-or-later
// AdvancedSourceInterface — adds per-player directDataPacket / putPacket
// / close to the base SourceInterface. Mirrors PocketMine's
// AdvancedSourceInterface.php.
#pragma once

#include "SourceInterface.h"
#include <string>
#include <cstdint>
#include <vector>

namespace atomikpe {

class Player;

namespace network {

class AdvancedSourceInterface : public SourceInterface {
public:
    // Send a packet (already-encoded bytes including the leading ID byte)
    // to the player identified by `ip`:`port`.
    virtual void sendRawPacket(const std::string& ip, uint16_t port,
                                 const std::vector<uint8_t>& data) = 0;
    // Close a session immediately, optionally sending a DisconnectPacket first.
    virtual void closeSession(const std::string& ip, uint16_t port,
                                const std::string& reason = "") = 0;
};

} // namespace network
} // namespace atomikpe
