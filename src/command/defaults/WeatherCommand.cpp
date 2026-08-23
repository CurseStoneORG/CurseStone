#include "WeatherCommand.h"
#include "Server.h"
namespace cenisyspe {
bool WeatherCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>&) {
    sender->sendMessage("/weather is not implemented yet");
    return true;
}
} // namespace
