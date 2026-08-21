#include "ConsoleCommandSender.h"
#include "utils/MainLogger.hpp"
namespace mcppeserver {
void ConsoleCommandSender::sendMessage(const std::string& msg) {
    utils::MainLogger::get().info(msg);
}
} // namespace
