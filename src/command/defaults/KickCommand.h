#pragma once
#include "command/Command.h"
#include <string>
namespace cursestone {
class KickCommand : public Command {
public:
    KickCommand() : Command("kick", "Kick a player", "", "cursestone.command.kick") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
