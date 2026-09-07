// ServerCommandEvent — fired when the console issues a command.
#pragma once
#include "event/Event.h"
#include "event/Cancellable.h"
#include <string>
namespace atomikpe { class CommandSender; }
namespace atomikpe::event::server {
class ServerCommandEvent : public Event, public Cancellable {
public:
    atomikpe::CommandSender* sender;
    std::string command;
    ServerCommandEvent(atomikpe::CommandSender* s, std::string cmd)
        : sender(s), command(std::move(cmd)) {}
    const std::string& eventName() const override {
        static std::string n = "ServerCommandEvent"; return n;
    }
};
} // namespace
