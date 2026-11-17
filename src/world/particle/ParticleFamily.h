/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

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
// ParticleFamily — the payload families of the protocol-84 particle
// registry. Every particle header declares which family it belongs to
// (kDataKind); this header carries the shared packing helpers and the
// acceptance check World::spawnParticle applies before broadcasting.
#pragma once
#include "ParticleId.h"
#include <cstdint>

namespace cursestone::world::particle {

/// @brief The payload family a particle id reads on the client.
enum class DataKind : uint8_t {
    None,  ///< payload ignored by the client renderer
    Scale, ///< payload = scale / lifetime integer
    Color, ///< payload = packed RGB
    Item,  ///< payload = item packing of ItemBreakParticle
    Block, ///< payload = block packing of TerrainParticle
};

/// @brief Protocol-84 item-break packing: item id in bits 16+, damage in
///        the low 16 bits (reference ItemBreakParticle).
inline int32_t packItemData(uint16_t itemId, uint16_t damage) {
    return ((int32_t)itemId << 16) | (int32_t)damage;
}

/// @brief Protocol-84 terrain packing: block id in the low byte, meta in
///        bits 8..11 (reference TerrainParticle).
inline int32_t packBlockData(uint8_t blockId, uint8_t meta) {
    return ((int32_t)(meta & 0x0F) << 8) | (int32_t)blockId;
}

/// @brief True when the 0.15.10 client can render @p data for @p id.
///
/// The structured families validate in their own particle files: an item
/// break of an unknown item (item id 0 on the wire included) and a terrain
/// burst of air both crash the client, so World::spawnParticle rejects
/// them instead of broadcasting. Every other family accepts the whole
/// int32 range.
bool isAcceptableParticleData(ParticleId id, int32_t data);

} // namespace cursestone::world::particle
