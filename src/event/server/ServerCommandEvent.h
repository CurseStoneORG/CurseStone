// ServerCommandEvent — fired when the console issues a command.
#pragma once
#include "Event.h"
#include "Cancellable.h"
#include <string>
namespace cenisyspe { class CommandSender; }
namespace cenisyspe::event::server {
class ServerCommandEvent : public Event, public Cancellable {
public:
    cenisyspe::CommandSender* sender;
    std::string command;
    ServerCommandEvent(cenisyspe::CommandSender* s, std::string cmd)
        : sender(s), command(std::move(cmd)) {}
    const std::string& eventName() const override {
        static std::string n = "ServerCommandEvent"; return n;
    }
};
} // namespace
