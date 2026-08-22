#pragma once
#include "command/Command.h"
#include <string>
namespace mcppeserver {
class GamemodeCommand : public Command {
public:
    GamemodeCommand() : Command("gamemode", "Set gamemode") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
