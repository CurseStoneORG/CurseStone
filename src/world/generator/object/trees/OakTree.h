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
// OakTree — vanilla small oak (the GenericTree default shape).
// Ported from ext-vanillagenerator's GenericTree default species.
#pragma once
#include "GenericTree.h"

namespace cursestone::world::generator::object::trees {

class OakTree : public GenericTree {
protected:
    void init(MersenneRandom& rng) override {
        height_ = 4 + (int)rng.nextInt(3);
        logId_ = 17;  logData_ = 0;   // LOG, oak
        leafId_ = 18; leafData_ = 0;  // LEAVES, oak
    }
};

} // namespace cursestone::world::generator::object::trees
