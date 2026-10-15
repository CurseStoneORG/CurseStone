/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
#include "OfflinePlayer.h"
#include "Server.h"
#include "utils/MainLogger.hpp"
namespace cursestone {
void OfflinePlayer::sendMessage(const std::string& msg) {
    // Offline players have no session — log the message at debug level.
    utils::MainLogger::get().debug("[OfflinePlayer:" + name_ + "] " + msg);
}
bool OfflinePlayer::hasPermission(const std::string&) const {
    return false;
}
bool OfflinePlayer::isOp() const {
    return srv_ && srv_->isOp(name_);
}
void OfflinePlayer::setOp(bool v) {
    if (srv_)
        srv_->setOp(name_, v);
}
bool OfflinePlayer::isBanned() const {
    return false;
}
void OfflinePlayer::setBanned(bool) {
}
bool OfflinePlayer::isWhitelisted() const {
    return false;
}
void OfflinePlayer::setWhitelisted(bool) {
}
} // namespace
