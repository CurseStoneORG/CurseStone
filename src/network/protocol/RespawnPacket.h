// SPDX-License-Identifier: LGPL-3.0-or-later
// RespawnPacket — clientbound. Le dice al cliente "reaparecé en esta
// posicion" (dispara la transicion fuera de la pantalla de muerte).
// Wire format (protocol 84, mirrors PMMP legacy RespawnPacket):
//   [Float BE] x
//   [Float BE] y
//   [Float BE] z
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class RespawnPacket : public DataPacket {
public:
    uint8_t pid() const override { return RESPAWN_PACKET; }

    float x = 0.0f, y = 0.0f, z = 0.0f;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
