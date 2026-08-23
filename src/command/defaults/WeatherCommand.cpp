#include "WeatherCommand.h"
#include "Server.h"
namespace mcppeserver {
bool WeatherCommand::execute(CommandSender* sender, const std::string& /*label*/,
                    const std::vector<std::string>& /*args*/) {
    sender->sendMessage("weather: not implemented yet");
    return true;
}
} // namespace
