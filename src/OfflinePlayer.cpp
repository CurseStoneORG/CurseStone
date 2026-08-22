#include "OfflinePlayer.h"
#include "Server.h"
namespace mcppeserver {
bool OfflinePlayer::isOp() const { return false; }
void OfflinePlayer::setOp(bool) {}
bool OfflinePlayer::isBanned() const { return false; }
void OfflinePlayer::setBanned(bool) {}
bool OfflinePlayer::isWhitelisted() const { return false; }
void OfflinePlayer::setWhitelisted(bool) {}
} // namespace
