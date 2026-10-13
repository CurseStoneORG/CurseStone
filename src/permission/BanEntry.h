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
// BanEntry — a single ban record. Has target (name or IP), reason,
// expiration (0 = permanent), and source.
#pragma once
#include <string>
#include <cstdint>
namespace cursestone::permission {
class BanEntry {
public:
    BanEntry(std::string target, std::string reason = "Banned", int64_t expires = 0)
        : target_(std::move(target)), reason_(std::move(reason)), expires_(expires) {}
    const std::string& target() const { return target_; }
    const std::string& reason() const { return reason_; }
    int64_t expires() const { return expires_; }
    bool isExpired() const;
private:
    std::string target_;
    std::string reason_;
    int64_t expires_;
    std::string source_ = "Server";
};
} // namespace
