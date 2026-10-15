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
// IPlayer — interface implemented by Player and OfflinePlayer.
// Mirrors pocketmine/IPlayer.php.
#pragma once
#include <string>
#include "command/CommandSender.h"
namespace cursestone {
class IPlayer : public CommandSender {
  public:
    virtual bool isOnline() const = 0;
    virtual void setOp(bool v) = 0;
    virtual bool isBanned() const = 0;
    virtual void setBanned(bool v) = 0;
    virtual bool isWhitelisted() const = 0;
    virtual void setWhitelisted(bool v) = 0;
};
} // namespace
