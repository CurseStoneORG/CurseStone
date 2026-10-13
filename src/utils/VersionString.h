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
// SPDX-License-Identifier: LGPL-3.0-or-later
// VersionString — semantic version wrapper used by Server/Plugin manifests.
#pragma once

#include <string>
#include <cstdint>

namespace cursestone::utils {

class VersionString {
public:
    VersionString() = default;
    explicit VersionString(const std::string& s);

    int major() const { return major_; }
    int minor() const { return minor_; }
    int patch() const { return patch_; }
    const std::string& suffix() const { return suffix_; }

    // True if this version is >= `other`.
    bool operator>=(const VersionString& other) const;
    bool operator==(const VersionString& other) const;
    bool operator<(const VersionString& other) const;

    std::string toString() const;

private:
    int         major_ = 0;
    int         minor_ = 0;
    int         patch_ = 0;
    std::string suffix_;
};

} // namespace cursestone::utils
