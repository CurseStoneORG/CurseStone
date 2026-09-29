// ServerCommandEvent — fired when the console issues a command.
#pragma once
#include "event/Event.h"
#include "event/Cancellable.h"
#include <string>
namespace cursestone { class CommandSender; }
namespace cursestone::event::server {
class ServerCommandEvent : public Event, public Cancellable {
public:
    cursestone::CommandSender* sender;
    std::string command;
    ServerCommandEvent(cursestone::CommandSender* s, std::string cmd)
        : sender(s), command(std::move(cmd)) {}
    const std::string& eventName() const override {
        static std::string n = "ServerCommandEvent"; return n;
    }
};
} // namespace
