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
// PlayerBucketEvent.h — ported from cn.nukkit.event.player.PlayerBucketEvent
//
// Base class for player bucket events (fill / empty). Holds the
// bucket ItemStack and the targeted block position + face.
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
namespace cursestone { class Player; }

namespace cursestone::event::player {
class PlayerBucketEvent : public PlayerEvent, public Cancellable {
public:
    cursestone::item::ItemStack bucket;
    int blockX = 0;
    int blockY = 0;
    int blockZ = 0;
    int face = 0;
    PlayerBucketEvent(cursestone::Player* p, const cursestone::item::ItemStack& b,
                      int bx, int by, int bz, int f)
        : PlayerEvent(p), bucket(b), blockX(bx), blockY(by), blockZ(bz), face(f) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerBucketEvent"; return n;
    }
};
} // namespace cursestone::event::player
