#pragma once
#include "command/Command.h"
#include <string>
namespace cursestone {
class VersionCommand : public Command {
public:
    VersionCommand() : Command("version", "Show server version info") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
