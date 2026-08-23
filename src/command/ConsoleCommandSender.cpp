#include "ConsoleCommandSender.h"
#include "utils/MainLogger.hpp"
#include "utils/TextColor.h"
#include <iostream>

namespace cenisyspe {

// Check if stdout is a terminal (supports ANSI colors)
static bool isTerminal() {
    return isatty(fileno(stdout)) != 0;
}

void ConsoleCommandSender::sendMessage(const std::string& msg) {
    // Convert MCPE color codes to ANSI for terminal output
    bool ansi = isTerminal();
    std::string output = TC::toANSI(msg, ansi);
    std::cout << output << std::endl;
}

} // namespace
