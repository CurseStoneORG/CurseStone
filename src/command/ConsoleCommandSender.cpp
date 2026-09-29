#include "ConsoleCommandSender.h"
#include "utils/MainLogger.hpp"
namespace cursestone {
void ConsoleCommandSender::sendMessage(const std::string& msg) {
    utils::MainLogger::get().info(msg);
}
} // namespace
