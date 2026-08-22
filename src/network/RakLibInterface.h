// SPDX-License-Identifier: LGPL-3.0-or-later
// RakLibInterface — C++ wrapper around the C raklib/RakLibServer.
// Protocol 84 / MCPE 0.15.10 compatible.
#pragma once

#include "AdvancedSourceInterface.h"
#include "raklib/RakLibServer.h"
#include "raklib/ServerInstance.h"
#include "raklib/SessionManager.h"
#include "../main.h"
#include <string>
#include <vector>

namespace mcppeserver {
class Server;

namespace network {

class RakLibInterface : public AdvancedSourceInterface {
public:
    explicit RakLibInterface(Server* server);
    ~RakLibInterface() override;

    std::string getName() const override { return "raklib"; }
    void start() override;
    void shutdown() override;
    void tick() override;

    // AdvancedSourceInterface
    void sendRawPacket(const std::string& ip, uint16_t port,
                         const std::vector<uint8_t>& data) override;
    void closeSession(const std::string& ip, uint16_t port,
                        const std::string& reason) override;

    // Update the MOTD / server name broadcasted in UNCONNECTED_PONG
    void setServerName(const std::string& motd);

    // C-to-C++ callbacks
    void onOpenSession(const std::string& ip, uint16_t port, int64_t clientId);
    void onCloseSession(const std::string& ip, uint16_t port,
                          const std::string& reason);
    void onPacketReceive(const std::string& ip, uint16_t port,
                          const uint8_t* data, size_t len);
    void onRawPacket(const std::string& ip, uint16_t port,
                       const uint8_t* data, size_t len);
    void onTick();

    int64_t getServerId() const { return serverId_; }
    bool    isRunning()   const { return running_; }

private:
    static void c_onOpen  (void* impl, const char* ip, uint16_t port, int64_t cid);
    static void c_onClose (void* impl, const char* ip, uint16_t port, const char* reason);
    static void c_onPacket(void* impl, const char* ip, uint16_t port,
                            const uint8_t* data, size_t len);
    static void c_onRaw   (void* impl, const char* ip, uint16_t port,
                            const uint8_t* data, size_t len);
    static void c_onTick  (void* impl);

    Server*               server_;
    mcp_raklib_server_t   raklib_;
    mcp_server_instance_t instance_;
    int64_t               serverId_ = 0;
    bool                  running_  = false;
};

} // namespace network
} // namespace mcppeserver
