#pragma once
#include "command/Command.h"
#include <string>
namespace cursestone {
class WeatherCommand : public Command {
public:
    WeatherCommand() : Command("weather", "Set weather") {}
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;
};
} // namespace
