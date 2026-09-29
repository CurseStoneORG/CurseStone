#pragma once
#include "command/Command.h"
#include <string>
namespace cursestone {
class SaveCommand : public Command {
public:
    SaveCommand() : Command("save", "Save worlds") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
