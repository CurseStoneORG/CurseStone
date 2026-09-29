#pragma once
#include "command/Command.h"
#include <string>
namespace cursestone {
class GamemodeCommand : public Command {
public:
    GamemodeCommand(const std::string& name = "gamemode") : Command(name, "Set gamemode") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
