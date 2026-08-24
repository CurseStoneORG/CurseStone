// SPDX-License-Identifier: LGPL-3.0-or-later
// RCON.cpp. C++.
#include "RCON.h"
#include "RCONInstance.h"
#include "../../Server.h"
#include "../../main.h"

namespace atomikpe {
namespace network {

RCON::RCON(Server* server) : server_(server) {}

RCON::~RCON() { shutdown(); }

void RCON::start() {
    if (instance_) return;
    instance_ = std::make_unique<RCONInstance>(server_);
    instance_->start();
}

void RCON::shutdown() {
    if (instance_) {
        instance_->shutdown();
        instance_.reset();
    }
}

void RCON::tick() {
    if (instance_) instance_->tick();
}

} // namespace network
} // namespace atomikpe
