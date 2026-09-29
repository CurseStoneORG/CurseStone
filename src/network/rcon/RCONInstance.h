// SPDX-License-Identifier: LGPL-3.0-or-later
// RCONInstance — single listening socket + accept loop. C++.
// Stub for now: actual TCP listen + per-connection thread will be
// implemented in Phase 21 (full server wiring).
#pragma once

#include "../../main.h"
#include <memory>
#include <atomic>

namespace cursestone {
class Server;

namespace network {

class RCONInstance {
public:
    explicit RCONInstance(Server* server) : server_(server) {}
    ~RCONInstance() { shutdown(); }

    void start()  { running_ = true; }
    void shutdown() { running_ = false; }
    void tick()    { /* TODO: accept TCP connections, parse packets */ }

private:
    Server*       server_;
    std::atomic<bool> running_{false};
};

} // namespace network
} // namespace cursestone
