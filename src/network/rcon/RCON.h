// SPDX-License-Identifier: LGPL-3.0-or-later
// RCON — Source RCON server. Listens for password-authenticated
// console commands. C++ facade over a per-connection instance.
#pragma once

#include "../SourceInterface.h"
#include "../../main.h"
#include <string>
#include <memory>
#include <vector>

namespace cenisyspe {
class Server;

namespace network {

class RCONInstance;

class RCON : public SourceInterface {
public:
    explicit RCON(Server* server);
    ~RCON() override;

    std::string getName() const override { return "rcon"; }
    void start() override;
    void shutdown() override;
    void tick() override;

private:
    Server* server_;
    std::unique_ptr<RCONInstance> instance_;
};

} // namespace network
} // namespace cenisyspe
