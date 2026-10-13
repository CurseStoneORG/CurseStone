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
//
// Synth — base class of the vanilla noise.
//
// Copiado 1:1 de `handheld/src/world/level/levelgen/synth/Synth.h`
// (SSMGAlt/PocketEdition): el `create()` que llena una rejilla evaluando
// getValue() por celda.
#pragma once

namespace cursestone::world::generator {

class Synth {
public:
    virtual ~Synth();

    int getDataSize(int width, int height);

    virtual float getValue(float x, float y) = 0;

    void create(int width, int height, float* result);
};

} // namespace cursestone::world::generator
