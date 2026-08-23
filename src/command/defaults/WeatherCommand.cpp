#include "WeatherCommand.h"
#include "Server.h"
namespace mcppeserver {
bool WeatherCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>&) {
    sender->sendMessage("\xc2\xa7c/weather is not implemented yet");
    return true;
}
} // namespace
