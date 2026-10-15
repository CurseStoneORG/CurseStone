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
#include "world/generator/noise/Synth.h"

namespace cursestone::world::generator {

Synth::~Synth() {
}

int Synth::getDataSize(int width, int height) {
    return width * height * sizeof(float);
}

void Synth::create(int width, int height, float* result) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            result[x + y * width] = getValue((float)x, (float)y);
        }
    }
}

} // namespace cursestone::world::generator
