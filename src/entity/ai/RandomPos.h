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
// RandomPos — the wander/panic position sampler.
//
// The MCPE 0.8.1 decompile ships <util/RandomPos.hpp> with no body (the
// PanicGoal and RandomStrollGoal call sites survive: generateRandomPos(
// out, holder, xzRange, yRange, flag)). The port reconstructs the classic
// era behavior those call sites need: sample up to ten random offsets in
// the requested range, snap each to the ground, and accept the first one
// that is walkable (solid floor, two free body cells) and far enough
// from the mob to be worth a walk.
#pragma once
#include <cstdint>

namespace cursestone::entity::ai {

class Mob;

/// @brief The era generateRandomPos contract. @return true when @p outX/
///        @p outY / @p outZ received a usable ground position.
bool generateRandomPos(double& outX, double& outY, double& outZ, Mob& mob,
                       int32_t xzRange, int32_t yRange);

} // namespace cursestone::entity::ai
