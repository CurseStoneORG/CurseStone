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
#pragma once
#include "IPlayer.h"
#include <string>
namespace cursestone {
class Server;
class OfflinePlayer : public IPlayer {
  public:
    OfflinePlayer(class Server* srv, std::string name) : srv_(srv), name_(std::move(name)) {
    }
    void sendMessage(const std::string& msg) override;
    std::string getName() const override {
        return name_;
    }
    bool hasPermission(const std::string&) const override;
    bool isOnline() const override {
        return false;
    }
    bool isOp() const override;
    void setOp(bool v) override;
    bool isBanned() const override;
    void setBanned(bool v) override;
    bool isWhitelisted() const override;
    void setWhitelisted(bool v) override;

  private:
    Server* srv_;
    std::string name_;
};
} // namespace
