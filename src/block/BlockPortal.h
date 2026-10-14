/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
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
#include "Block.h"
#include <string>
#include "BlockIds.h"

namespace cursestone::block {

/// @brief block of portal of the Nether (id 90).
///
/// fills the interior of a frame of obsidian. not is solid (the
/// player it passes through when entering) and in the meta saves the axis of the
/// portal: 0 to it along of X, 1 to it along of Z.
class BlockPortal : public Block {
public:
    uint8_t id() const override { return NETHER_PORTAL; }
    std::string name() const override { return "Nether Portal"; }
    bool isSolid() const override { return false; }
    bool isTransparent() const override { return true; }
};

} // namespace cursestone::block
