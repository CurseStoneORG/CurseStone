#pragma once
#include "command/Command.h"
#include <string>
namespace mcppeserver {
class SayCommand : public Command {
public:
    SayCommand() : Command("say", "Broadcast a message") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
