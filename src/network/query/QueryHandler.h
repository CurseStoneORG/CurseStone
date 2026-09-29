// SPDX-License-Identifier: LGPL-3.0-or-later
// QueryHandler — GS4 status query (Source Engine Query protocol).
// Answers "ping" requests sent by external server list services.
// C++.
#pragma once

#include "../SourceInterface.h"
#include "../../main.h"
#include "../../utils/UUID.h"
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <chrono>

namespace cursestone {
class Server;

namespace network {

class QueryHandler : public SourceInterface {
public:
    explicit QueryHandler(Server* server) : server_(server) {}

    std::string getName() const override { return "query"; }
    void start() override;
    void shutdown() override {}
    void tick() override;

    // Handle a raw UDP datagram (received by RakLib as a raw packet and
    // forwarded here). Returns true if `data` was a query packet.
    bool handlePacket(const std::string& ip, uint16_t port,
                       const uint8_t* data, size_t len);

private:
    // Generate the per-server 16-byte magic token for the handshake.
    void regenerateToken();

    Server* server_;
    bool    running_ = false;
    std::vector<uint8_t> token_ = std::vector<uint8_t>(16, 0);
    std::unordered_map<std::string,
                       std::chrono::steady_clock::time_point> challenges_;
};

} // namespace network
} // namespace cursestone
