// SPDX-License-Identifier: LGPL-3.0-or-later
// AddEntityPacket.h — spawn a non-player entity on the client.
//
// Wire format (protocol 84, verified against Dr1xyDev/Infinity
// AddEntityPacket.php — the MCPE 0.15.10 reference):
//   [Long  BE] eid
//   [Int   BE] type        (entity network id, e.g. 93 = Lightning)
//   [Float BE] x, y, z
//   [Float BE] speedX, speedY, speedZ
//   [Float BE] yaw         (Infinity writes yaw * 0.71111)
//   [Float BE] pitch       (Infinity writes pitch * 0.71111)
//   [Int   BE] modifiers
//   [metadata blob]        (empty = single 0 byte)
//   [Short BE] links count + links (we never send links)
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class AddEntityPacket : public DataPacket {
public:
    uint8_t pid() const override { return ADD_ENTITY_PACKET; }

    // Entity type network ids (Infinity Entity.php).
    static constexpr int32_t TYPE_LIGHTNING = 93;

    int64_t eid = 0;
    int32_t type = 0;
    float x = 0, y = 0, z = 0;
    float speedX = 0, speedY = 0, speedZ = 0;
    float yaw = 0, pitch = 0;
    int32_t modifiers = 0;
    bool yawPitchScaled = true;   // Infinity multiplies both by 0.71111

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
