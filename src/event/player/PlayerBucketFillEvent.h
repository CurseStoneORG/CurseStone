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
// PlayerBucketFillEvent.h — fired when a player with an empty bucket
// (BUCKET aux 0) is about to scoop a liquid SOURCE cell (the cell must
// carry the static liquid form, not a flowing part).
//
// Timing: the event runs after the source cell has been identified but
// BEFORE the world block is removed and BEFORE the held bucket is
// swapped for the filled variant. Cancelling keeps both the world and
// the inventory exactly as they were and the client's target cell is
// resynced — no partial fill, no lost or duplicated buckets.
//
// This event stays separate from its base PlayerBucketEvent: listeners
// registered for the base class see pour operations, this class only
// reaches fill listeners (plus any listener registered on the base
// through an explicit subclass hook).
#pragma once
#include "PlayerBucketEvent.h"
namespace cursestone {
class Player;
}

namespace cursestone::event::player {

class PlayerBucketFillEvent : public PlayerBucketEvent {
  public:
    PlayerBucketFillEvent(cursestone::Player* p, const cursestone::item::ItemStack& bucket,
                          int32_t liquidId, int32_t x, int32_t y, int32_t z)
        : PlayerBucketEvent(p, bucket, liquidId, x, y, z, BucketAction::SCOOP) {
    }

    const std::string& eventName() const override {
        static std::string n = "PlayerBucketFillEvent";
        return n;
    }
};

} // namespace cursestone::event::player
