#pragma once
#include "command/Command.h"
#include <string>
namespace cursestone {
class TeleportCommand : public Command {
public:
    TeleportCommand(const std::string& name = "teleport") : Command(name, "Teleport player", "", "cursestone.command.teleport") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
