#include "TimeCommand.h"
#include "Server.h"
#include "level/Level.h"
#include "network/protocol/SetTimePacket.h"
#include "Player.h"

namespace mcppeserver {
bool TimeCommand::execute(CommandSender* sender, const std::string& /*label*/,
                    const std::vector<std::string>& args) {
    if (args.empty()) {
        auto* lvl = Server::getInstance().getDefaultLevel();
        if (lvl) sender->sendMessage("§eCurrent time: " + std::to_string(lvl->time()));
        return true;
    }
    if (args[0] == "set" && args.size() >= 2) {
        int32_t t;
        if (args[1] == "day") t = 1000;
        else if (args[1] == "night") t = 13000;
        else { try { t = std::stoi(args[1]); } catch (...) { sender->sendMessage("§cInvalid time"); return true; } }
        auto* lvl = Server::getInstance().getDefaultLevel();
        if (lvl) {
            lvl->setTime(t);
            // Broadcast to all players
            auto pk = std::make_shared<network::protocol::SetTimePacket>();
            pk->time = t;
            pk->started = true;
            for (auto& p : Server::getInstance().players()) {
                if (p) p->sendPacket(pk);
            }
            sender->sendMessage("§aSet time to " + std::to_string(t));
        }
        return true;
    }
    if (args[0] == "add" && args.size() >= 2) {
        int32_t t;
        try { t = std::stoi(args[1]); } catch (...) { sender->sendMessage("§cInvalid value"); return true; }
        auto* lvl = Server::getInstance().getDefaultLevel();
        if (lvl) {
            lvl->advanceTime(t);
            sender->sendMessage("§aAdded " + std::to_string(t) + " to time");
        }
        return true;
    }
    sender->sendMessage("§cUsage: /time <set|add|get> [value]");
    return true;
}
} // namespace
