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
#include "PermissibleBase.h"
namespace cursestone::permission {
bool PermissibleBase::hasPermission(const std::string& name) const {
    // Exact match
    if (auto it = perms_.find(name); it != perms_.end())
        return it->second;
    // Wildcard walk up the tree.
    std::string cur = name;
    while (true) {
        auto dot = cur.rfind('.');
        if (dot == std::string::npos)
            break;
        cur = cur.substr(0, dot) + ".*";
        if (auto it = perms_.find(cur); it != perms_.end())
            return it->second;
        cur.pop_back();
        cur.pop_back(); // strip ".*"
    }
    return false;
}
void PermissibleBase::setPermission(const std::string& name, bool value) {
    perms_[name] = value;
}
void PermissibleBase::removePermission(const std::string& name) {
    perms_.erase(name);
}
void PermissibleBase::recalculatePermissions() {
    perms_.clear();
    for (auto& a : attachments_) {
        for (auto& kv : a->permissions())
            perms_[kv.first] = kv.second;
    }
}
} // namespace
