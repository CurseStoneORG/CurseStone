#pragma once
#include "command/Command.h"
#include <string>
namespace atomikpe {
class TeleportCommand : public Command {
public:
    TeleportCommand(const std::string& name = "teleport") : Command(name, "Teleport player") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
