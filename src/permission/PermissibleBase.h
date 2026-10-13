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
// PermissibleBase — default Permissible implementation. Holds a
// map of permission nodes to boolean values. Wildcards are
// expanded lazily on lookup.
#pragma once
#include "Permissible.h"
#include <string>
#include "PermissionAttachment.h"
#include <unordered_map>
#include <memory>
#include <vector>
namespace cursestone::permission {
class PermissibleBase : public Permissible {
public:
    bool hasPermission(const std::string& name) const override;
    void recalculatePermissions() override;
    void setPermission(const std::string& name, bool value);
    void removePermission(const std::string& name);
    PermissionAttachment* attach(std::shared_ptr<PermissionAttachment> a) {
        attachments_.push_back(a);
        recalculatePermissions();
        return a.get();
    }
private:
    std::unordered_map<std::string, bool> perms_;
    std::vector<std::shared_ptr<PermissionAttachment>> attachments_;
};
} // namespace
