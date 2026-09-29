#include "HelpCommand.h"
#include "Server.h"
#include "command/SimpleCommandMap.h"
#include "command/Command.h"
#include <algorithm>
#include <vector>

namespace cursestone {

// Helper: get all registered commands sorted by name
struct CmdEntry {
    std::string name;
    std::string description;
};

bool HelpCommand::execute(CommandSender* sender, const std::string&,
                    const std::vector<std::string>& args) {
    // Parse page number from args
    int pageNumber = 1;
    if (!args.empty()) {
        try { pageNumber = std::stoi(args.back()); if (pageNumber < 1) pageNumber = 1; } catch (...) {}
    }

    // Build command list (sorted alphabetically)
    // Since SimpleCommandMap doesn't expose iteration, we list all known commands
    std::vector<CmdEntry> commands = {
        {"deop", "Remove operator status"},
        {"gamemode", "Set gamemode (0=survival, 1=creative, 2=adventure)"},
        {"give", "Give items to a player"},
        {"gm", "Alias for /gamemode"},
        {"help", "Show this help page"},
        {"kick", "Kick a player"},
        {"list", "List online players"},
        {"op", "Give operator status"},
        {"save", "Save the world"},
        {"say", "Broadcast a message"},
        {"stop", "Stop the server"},
        {"teleport", "Teleport to coordinates"},
        {"time", "Set or get the world time"},
        {"tp", "Alias for /teleport"},
        {"version", "Show server version info"},
        {"weather", "Change the weather"},
        {"?", "Alias for /help"},
    };
    std::sort(commands.begin(), commands.end(), [](const CmdEntry& a, const CmdEntry& b) {
        return a.name < b.name;
    });

    // Paginate: 7 commands per page (like PMMP)
    int perPage = 7;
    int totalPages = (int)((commands.size() + perPage - 1) / perPage);
    if (pageNumber > totalPages) pageNumber = totalPages;

    // Header (PMMP style: "--- Showing help page X/Y ---")
    sender->sendMessage("--- Showing help page " + std::to_string(pageNumber) + "/" + std::to_string(totalPages) + " ---");

    // Show commands for this page
    int start = (pageNumber - 1) * perPage;
    int end = std::min(start + perPage, (int)commands.size());
    for (int i = start; i < end; ++i) {
        sender->sendMessage("/" + commands[i].name + ": " + commands[i].description);
    }

    return true;
}

} // namespace
