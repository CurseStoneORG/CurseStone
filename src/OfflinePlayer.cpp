#include "OfflinePlayer.h"
#include "Server.h"
#include "utils/MainLogger.hpp"
namespace mcppeserver {
void OfflinePlayer::sendMessage(const std::string& msg) {
    // Offline players have no session — log the message at debug level.
    utils::MainLogger::get().debug("[OfflinePlayer:" + name_ + "] " + msg);
}
bool OfflinePlayer::hasPermission(const std::string&) const { return false; }
bool OfflinePlayer::isOp() const {
    return srv_ && srv_->isOp(name_);
}
void OfflinePlayer::setOp(bool v) {
    if (srv_) srv_->setOp(name_, v);
}
bool OfflinePlayer::isBanned() const { return false; }
void OfflinePlayer::setBanned(bool) {}
bool OfflinePlayer::isWhitelisted() const { return false; }
void OfflinePlayer::setWhitelisted(bool) {}
} // namespace
