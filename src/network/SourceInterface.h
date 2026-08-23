// SPDX-License-Identifier: LGPL-3.0-or-later
// SourceInterface — abstract network interface. C++.
// A Network may have multiple SourceInterfaces (RakLib for client traffic,
// QueryHandler for GS4 status, RCON for remote console).
#pragma once

#include <string>
#include <cstdint>

namespace cenisyspe::network {

class SourceInterface {
public:
    virtual ~SourceInterface() = default;
    virtual std::string getName() const = 0;
    virtual void start() = 0;
    virtual void shutdown() = 0;
    virtual void tick() = 0;
};

} // namespace cenisyspe::network
