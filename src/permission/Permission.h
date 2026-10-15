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
// Permission — a single permission node (e.g. "cursestone.command.stop").
// Permissions form a tree with wildcards ("cursestone.command.*").
// Mirrors pocketmine/permission/Permission.php.
#pragma once
#include <string>
namespace cursestone::permission {
class Permission {
  public:
    explicit Permission(std::string name, std::string description = "")
        : name_(std::move(name)), description_(std::move(description)) {
    }
    const std::string& name() const {
        return name_;
    }
    const std::string& description() const {
        return description_;
    }
    // Default value if no Permissible has set one (true=op-only, false=default-allow).
    bool defaultValue() const {
        return false;
    }

  private:
    std::string name_;
    std::string description_;
};
} // namespace
