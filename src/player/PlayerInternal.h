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

// ============================================================
// PlayerInternal.h — shared implementation detail of the Player
// translation units (src/Player.cpp + src/player/*.cpp).
//
// SOLO uso interno: no incluir este header desde fuera de src/.
// Aqui viven las constantes y helpers `static`/anonimos que mas de
// uno de esos .cpp necesitan (kEyeHeight, makePoseFlagEntry).
// ============================================================
#include "network/protocol/SetEntityDataPacket.h"

namespace cursestone {

// Vanilla MCPE 0.15.10 player eye height (Human::getEyeHeight() == 1.62).
//
// RakNet's MovePlayerPacket and MoveEntityPacket carry the EYE altitude on
// the wire; EVERY other position-carrying packet (StartGame, AddPlayerPacket,
// SetSpawnPosition, Respawn) and the whole entity/physics model use the FEET.
// pos_ holds the FEET, exactly like Genisys' Entity::$y — so handleMovePlayer()
// subtracts this on receive and the two movement packets add it on send.
//
// Before this constant existed pos_ stored the raw wire value, i.e. the EYE,
// which left every feet-based computation 1.62 too high. The item-pickup
// volume (anchored on the feet) was the visible casualty: its 1.0-below-the-
// feet reach actually pointed 0.62 ABOVE the real feet, so items resting on
// the ground could never be collected while items floating at chest height
// could — the "only my upper half can pick things up" report.
constexpr double kEyeHeight = 1.62;

// Builds the metadata entry describing a player's pose:
// Entity::DATA_FLAGS = 0 (byte) — bit 0 = ON_FIRE, bit 1 = SNEAKING,
// bit 3 = SPRINTING (protocol 84 entity metadata). Protocol 84 clients
// render the burning effect (flames + fire overlay) from the ON_FIRE bit
// alone. All bits share the same property, so they always travel together
// as the full byte.
inline network::protocol::SetEntityDataPacket::Entry
makePoseFlagEntry(bool sneaking, bool sprinting, bool action = false, bool onFire = false) {
    network::protocol::SetEntityDataPacket::Entry e;
    e.index = network::protocol::SetEntityDataPacket::PROP_FLAGS;
    e.type = network::protocol::SetEntityDataPacket::TYPE_BYTE;
    uint8_t flags = 0;
    if (onFire)
        flags |= (1 << network::protocol::SetEntityDataPacket::FLAG_BIT_ONFIRE);
    if (sneaking)
        flags |= (1 << network::protocol::SetEntityDataPacket::FLAG_BIT_SNEAKING);
    if (sprinting)
        flags |= (1 << network::protocol::SetEntityDataPacket::FLAG_BIT_SPRINTING);
    if (action)
        flags |= (1 << network::protocol::SetEntityDataPacket::FLAG_BIT_ACTION);
    e.ival = flags;
    return e;
}
} // namespace cursestone
