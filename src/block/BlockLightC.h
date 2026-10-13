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
// BlockLightC.h — narrow C bridge into the Block C++ registry.
//
// Chunk.c (the light-propagation pass) is pure C and cannot include
// Block.h (C++ class hierarchy). These two functions are implemented
// in Block.cpp as `extern "C"` and give the C side just what it needs
// to run sky/block light propagation: how much light a block eats
// per step (0..15) and how much light it emits (0..15, torch/glowstone).
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 0 = fully transparent to light (air, leaves, glass, water, ...).
// 15 = fully opaque (ordinary solid block). Unknown ids are treated
// as opaque, matching Block::get()'s fallback to BlockStone.
uint8_t mcp_block_light_opacity(uint8_t block_id);

// 0 for ordinary blocks, up to 15 for light sources (torch, glowstone,
// lava, ...).
uint8_t mcp_block_light_emission(uint8_t block_id);

#ifdef __cplusplus
} // extern "C"
#endif
