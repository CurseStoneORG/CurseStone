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
// ContainerSetDataPacket — server→client "cambió una propiedad de un
// contenedor abierto" (MCPE protocol 84).
//
// Formato p84 verificado contra the reference implementation + Nukkit 2016:
// shorts BIG-endian, no varints:
//   putByte(windowid)
//   putShort(BE) property
//   putShort(BE) value
//
// Se usa para la UI del horno: flecha de progreso (property 0) y
// llama de fuego (property 1), solo mientras el jugador lo tiene
// abierto. Sin este packet el cliente NO muestra el progreso de
// fundición aunque el horno esté trabajando.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class ContainerSetDataPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return CONTAINER_SET_DATA_PACKET;
    }

    // Properties (vanilla): 0 = progreso de fundición (0..200),
    // 1 = remaining fuel burn (0..200). El cliente escala ambos a su
    // barra/llama visual.
    static constexpr int PROPERTY_FURNACE_TICKS = 0;
    static constexpr int PROPERTY_FURNACE_FUEL = 1;

    uint8_t windowid = 0;
    int32_t property = 0;
    int32_t value = 0;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
