// SPDX-License-Identifier: LGPL-3.0-or-later
// DataPacketReceiveEvent.h — ported from cn.nukkit.event.server.DataPacketReceiveEvent
//
// Fired when the server receives a packet from a player. Cancellable;
// cancelling the event discards the packet.
#pragma once
#include "ServerEvent.h"
#include "../Cancellable.h"
#include <string>
namespace atomikpe { class Player; }

namespace atomikpe::event::server {

class DataPacketReceiveEvent : public ServerEvent, public Cancellable {
public:
    void* packet;
    atomikpe::Player* player;

    DataPacketReceiveEvent(atomikpe::Player* p, void* pk)
        : packet(pk), player(p) {}

    const std::string& eventName() const override {
        static std::string n = "DataPacketReceiveEvent"; return n;
    }
};

} // namespace atomikpe::event::server
