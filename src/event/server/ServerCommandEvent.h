// ServerCommandEvent — fired when the console issues a command.
#pragma once
#include "Event.h"
#include "Cancellable.h"
#include <string>
namespace mcppeserver { class CommandSender; }
namespace mcppeserver::event::server {
class ServerCommandEvent : public Event, public Cancellable {
public:
    mcppeserver::CommandSender* sender;
    std::string command;
    ServerCommandEvent(mcppeserver::CommandSender* s, std::string cmd)
        : sender(s), command(std::move(cmd)) {}
    const std::string& eventName() const override {
        static std::string n = "ServerCommandEvent"; return n;
    }
};
} // namespace
