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
#include "Attribute.h"
#include <string>
#include <unordered_map>
namespace cursestone::entity {
class AttributeMap {
  public:
    void set(const Attribute& a) {
        map_[a.name()] = a;
    }
    Attribute get(const std::string& name) const {
        auto it = map_.find(name);
        return it == map_.end() ? Attribute(name, 0, 0, 0) : it->second;
    }
    const std::unordered_map<std::string, Attribute>& all() const {
        return map_;
    }

  private:
    std::unordered_map<std::string, Attribute> map_;
};
} // namespace
