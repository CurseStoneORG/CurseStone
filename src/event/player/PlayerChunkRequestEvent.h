// SPDX-License-Identifier: LGPL-3.0-or-later
// PlayerChunkRequestEvent.h — ported from cn.nukkit.event.player.PlayerChunkRequestEvent
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include <cstdint>
namespace cursestone { class Player; }

namespace cursestone::event::player {
class PlayerChunkRequestEvent : public PlayerEvent, public Cancellable {
public:
    int32_t chunkX = 0;
    int32_t chunkZ = 0;
    PlayerChunkRequestEvent(cursestone::Player* p, int32_t cx, int32_t cz)
        : PlayerEvent(p), chunkX(cx), chunkZ(cz) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerChunkRequestEvent"; return n;
    }
};
} // namespace cursestone::event::player
