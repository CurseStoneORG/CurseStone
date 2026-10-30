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
#include "BlockTorch.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockRedstoneTorch : public BlockTorch {
  public:
    // FIX: "REDSTONE_TORCH" (sin sufijo) no existe en BlockIds.h — solo
    // REDSTONE_TORCH_OFF (75) y REDSTONE_TORCH_ON (76), que son dos
    // bloques con id distinto (igual que MCPE/vanilla los modelan:
    // encender/apagar una antorcha de redstone cambia su id de bloque,
    // no solo su `data`). Esta clase representa el estado apagado —
    // BlockRedstoneTorchOn (if ever needed as its own class) would use
    // REDSTONE_TORCH_ON. The actual on/off logic lives in
    // RedstoneManager operando directamente sobre el id del mundo, no
    // this hierarchy (currently not wired into the
    // juego — ver BlockRegistry.cpp, donde nada llama a Block::get()).
    uint8_t id() const override {
        return REDSTONE_TORCH_OFF;
    }
    std::string name() const override {
        return "Redstone Torch";
    }
};
} // namespace
