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
// Angle helpers the era AI controls use (<minecraftpe/impl/math/Mth>).
//
// The MCPE 0.8.1 decompile calls Mth::wrapDegrees and Mth::clampRotate
// from BodyControl and LookControl; their bodies are not in the
// decompile, so the implementations here follow the semantics those call
// sites require (the classic era helpers):
//   - wrapDegrees(a) folds @p a into [-180, 180).
//   - clampRotate(cur, target, maxStep) moves @p cur toward @p target by
//     at most @p maxStep degrees, wrapping the difference.
//
// Conventions: every angle in the AI layer is the protocol-84 entity yaw
// the server already broadcasts (0 = +Z, 90 = +X), i.e. the movement
// direction of a yaw is (sin(yaw), cos(yaw)). The era decompile computes
// target yaws as atan2(dz, dx) - 90, which is the mirror image; the two
// conventions differ only by a sign, and every rotation the controls
// apply is a symmetric clamp, so porting the math in the protocol
// convention reproduces the era motion exactly (see the control
// comments for the per-site conversion).
#pragma once
#include <cmath>

namespace cursestone::entity::ai::math {

/// @brief Folds @p degrees into [-180, 180).
inline float wrapDegrees(float degrees) {
    float d = std::fmod(degrees, 360.0f);
    if (d >= 180.0f)
        d -= 360.0f;
    if (d < -180.0f)
        d += 360.0f;
    return d;
}

/// @brief Moves @p current toward @p target by at most @p maxStep
///        degrees (the era Mth::clampRotate contract).
inline float clampRotate(float current, float target, float maxStep) {
    const float delta = wrapDegrees(target - current);
    if (delta > maxStep)
        return current + maxStep;
    if (delta < -maxStep)
        return current - maxStep;
    return current + delta;
}

/// @brief Protocol-84 yaw of a horizontal direction (sin/cos convention).
inline float faceYaw(double dx, double dz) {
    return static_cast<float>(std::atan2(dx, dz) * (180.0 / 3.14159265358979323846));
}

/// @brief Movement direction of a protocol-84 yaw: (sin(yaw), cos(yaw)).
inline void yawDirection(float yawDeg, double& dx, double& dz) {
    const double rad = static_cast<double>(yawDeg) * (3.14159265358979323846 / 180.0);
    dx = std::sin(rad);
    dz = std::cos(rad);
}

/// @brief Era Mth::floor: the largest integer <= @p value as an int.
inline int floorToInt(double value) {
    const int i = static_cast<int>(value);
    return value < static_cast<double>(i) ? i - 1 : i;
}

} // namespace cursestone::entity::ai::math
