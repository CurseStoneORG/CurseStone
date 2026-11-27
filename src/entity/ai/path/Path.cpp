/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___/

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "entity/ai/path/Path.h"

namespace cursestone::entity::ai {

bool Path::sameAs(const Path* other) const {
    if (!other)
        return false;
    if (other->cells_.size() != cells_.size())
        return false;
    const size_t count = cells_.size();
    for (size_t i = 0; i < count; ++i) {
        const PathNode& a = cells_[i];
        const PathNode& b = other->cells_[i];
        if (a.xCoord != b.xCoord || a.yCoord != b.yCoord ||
            a.zCoord != b.zCoord)
            return false;
    }
    return true;
}

} // namespace cursestone::entity::ai
