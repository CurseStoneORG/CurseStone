// SPDX-License-Identifier: LGPL-3.0-or-later
// DataPacketSendEvent.h — ported from cn.nukkit.event.server.DataPacketSendEvent
//
// Fired before the server sends a packet to a player. Cancellable;
// cancelling the event prevents the packet from being sent.
#pragma once
#include "ServerEvent.h"
#include "../Cancellable.h"
#include <string>
namespace cursestone { class Player; }

namespace cursestone::event::server {

class DataPacketSendEvent : public ServerEvent, public Cancellable {
public:
    void* packet;
    cursestone::Player* player;

    DataPacketSendEvent(cursestone::Player* p, void* pk)
        : packet(pk), player(p) {}

    const std::string& eventName() const override {
        static std::string n = "DataPacketSendEvent"; return n;
    }
};

} // namespace cursestone::event::server
