#include "GiveCommand.h"
#include "Server.h"
#include "utils/TextColor.h"
namespace cenisyspe {
bool GiveCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>&) {
    sender->sendMessage(TC::RED + "/give is not implemented yet" + TC::RESET);
    return true;
}
} // namespace
