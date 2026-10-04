// SPDX-License-Identifier: LGPL-3.0-or-later
// SetEntityMotionPacket — server→client entity velocity (knockback).
//
// Wire format (protocol 84) — Genisys 0.15.0
// SetEntityMotionPacket::encode:
//
//     foreach($this->entities as $d){
//         $this->putLong($d[0]);   // eid
//         $this->putFloat($d[1]);  // motionX
//         $this->putFloat($d[2]);  // motionY
//         $this->putFloat($d[3]);  // motionZ
//     }
//
// No count field, no terminator — just the flat run of records. PMMP
// accumulates per-entity motion into this packet each tick
// (World::tick() -> `$this->motionToSend`), which is how knockback
// (Living::knockBack() -> setMotion()) reaches other clients: without
// it a hit only ever moved the victim's own client and everyone else
// watched them stand still.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class SetEntityMotionPacket : public DataPacket {
public:
    uint8_t pid() const override { return SET_ENTITY_MOTION_PACKET; }

    struct Motion {
        int64_t eid  = 0;
        float motX = 0.0f;
        float motY = 0.0f;
        float motZ = 0.0f;
    };

    std::vector<Motion> entities;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol