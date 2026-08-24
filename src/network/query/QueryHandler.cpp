// SPDX-License-Identifier: LGPL-3.0-or-later
// QueryHandler.cpp — GS4 status responses.
#include "QueryHandler.h"
#include "../../Server.h"
#include "../../utils/UUID.h"
#include "../../utils/Utils.h"
#include "../../utils/MainLogger.h"
#include "../../main.h"

#include <cstring>
#include <sstream>
#include <chrono>

namespace atomikpe {
namespace network {

void QueryHandler::start() {
    regenerateToken();
    running_ = true;
}

void QueryHandler::regenerateToken() {
    mcp_uuid_t u;
    if (mcp_uuid_v4(&u)) {
        token_.assign(u.bytes, u.bytes + 16);
    }
}

void QueryHandler::tick() {
    // No periodic work — query packets are handled synchronously in
    // handlePacket() when RakLib forwards them.
}

bool QueryHandler::handlePacket(const std::string& ip, uint16_t port,
                                   const uint8_t* data, size_t len) {
    if (!data || len < 7) return false;
    // GS4 magic: 0xFE 0xFD 0xFD 0xFE 0xFE 0xFE 0xFD (first 7 bytes).
    static const uint8_t MAGIC[7] = {0xFE, 0xFD, 0xFD, 0xFE, 0xFE, 0xFE, 0xFD};
    if (memcmp(data, MAGIC, 7) != 0) return false;

    // Byte 7 = session ID (4 bytes), byte 11 = packet type (0x00=handshake,
    // 0x00=status, 0x09=stat). Real GS4 is more complex; we stub it.
    (void)ip; (void)port;
    return true;  // Acknowledged, no response yet.
}

} // namespace network
} // namespace atomikpe
