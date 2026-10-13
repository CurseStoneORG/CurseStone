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
// EntityEventPacket — clientbound. Dispara animaciones/eventos
// cliente-side atados a una entidad (golpe recibido, muerte, etc).
// Wire format (protocol 84, mirrors PMMP EntityEventPacket):
//   [Int64 BE] eid
//   [Byte]     event
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class EntityEventPacket : public DataPacket {
public:
    uint8_t pid() const override { return ENTITY_EVENT_PACKET; }

    // Solo se usan los eventos que el pipeline de combate actual
    // dispara de verdad — el resto de la tabla de PMMP (tame,
    // fish-hook, ambient sound, etc.) no aplica todavia porque esas
    // mecanicas no existen en CurseStone.
    static constexpr uint8_t HURT_ANIMATION  = 2;
    static constexpr uint8_t DEATH_ANIMATION = 3;

    int64_t eid = 0;
    uint8_t event = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
