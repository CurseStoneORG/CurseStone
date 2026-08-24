#include "ConsoleCommandSender.h"
#include "utils/MainLogger.hpp"
namespace atomikpe {
void ConsoleCommandSender::sendMessage(const std::string& msg) {
    utils::MainLogger::get().info(msg);
}
} // namespace
