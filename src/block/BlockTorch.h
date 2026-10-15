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
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockTorch : public BlockTransparent {
  public:
    uint8_t id() const override {
        return TORCH;
    }
    std::string name() const override {
        return "Torch";
    }
    // Vanilla: una antorcha emite 14. Sin esto mcp_block_light_emission()
    // devolvia 0 y la antorcha no iluminaba NADA — una cueva iluminada
    // solo con antorchas quedaba completamente a oscuras.
    uint8_t getLightLevel() const override {
        return 14;
    }
};
} // namespace
