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
// PlayerBucketEvent.h — base event for every bucket operation a player
// performs. CurseStone supports two of them on the 0.15 wire, where one
// item id (BUCKET) covers all variants through its aux value:
//
//   - POUR  (aux 8 water / aux 10 lava): the held bucket places a liquid
//     source block into the world. The event fires BEFORE the shared
//     block-placement path runs, so cancelling leaves the world and the
//     held bucket untouched and the client is resynced.
//   - SCOOP (aux 0): an empty bucket drains a liquid source cell and
//     fills itself. SCOOP operations fire the specialised
//     PlayerBucketFillEvent (which derives from this class) instead —
//     keep both names registered separately in your listeners.
//
// The event carries the full bucket stack (id, aux/meta, count), the
// liquid block id involved and the target cell. Timing: before ANY
// world or inventory mutation, so a cancel can never leave a partial
// operation behind.
#pragma once
#include "PlayerEvent.h"
#include "../Cancellable.h"
#include "../../item/ItemStack.h"
#include <cstdint>
namespace cursestone {
class Player;
}

namespace cursestone::event::player {

class PlayerBucketEvent : public PlayerEvent, public Cancellable {
  public:
    // What the player is doing with the bucket.
    enum class BucketAction : uint8_t {
        POUR = 0,  // Filled bucket places its liquid into the world.
        SCOOP = 1, // Empty bucket drains a liquid source cell.
    };

    PlayerBucketEvent(cursestone::Player* p, const cursestone::item::ItemStack& bucket,
                      int32_t liquidId, int32_t x, int32_t y, int32_t z, BucketAction action)
        : PlayerEvent(p),
          bucket_(bucket),
          liquid_id_(liquidId),
          x_(x),
          y_(y),
          z_(z),
          action_(action) {
    }

    // The held bucket stack as the client presented it (id BUCKET with
    // the variant in meta/aux).
    const cursestone::item::ItemStack& getBucket() const {
        return bucket_;
    }
    // Block id of the liquid involved: 8 = water, 10 = lava.
    int32_t getLiquidId() const {
        return liquid_id_;
    }
    // Target cell: the cell receiving the poured liquid (POUR) or the
    // source cell being drained (SCOOP).
    int32_t getBlockX() const {
        return x_;
    }
    int32_t getBlockY() const {
        return y_;
    }
    int32_t getBlockZ() const {
        return z_;
    }
    // Which half of the bucket interaction this event describes.
    BucketAction getAction() const {
        return action_;
    }

    const std::string& eventName() const override {
        static std::string n = "PlayerBucketEvent";
        return n;
    }

  private:
    cursestone::item::ItemStack bucket_;
    int32_t liquid_id_;
    int32_t x_;
    int32_t y_;
    int32_t z_;
    BucketAction action_;
};

} // namespace cursestone::event::player
