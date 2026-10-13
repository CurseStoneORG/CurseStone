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
// BlockBurnEvent — block event.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
#include "math/Vector3.h"
#include "CurseStone.h"   // math::Vector3 alias (cursestone::math::Vector3 = mcp_vector3_t)
namespace cursestone { class Player; }
namespace cursestone::event::block {
class BlockBurnEvent : public Event, public Cancellable {
public:
    cursestone::Player* player;
    math::Vector3 pos;
    int block_id;
    BlockBurnEvent(cursestone::Player* p, math::Vector3 v, int id=0) : player(p), pos(v), block_id(id) {}
    const std::string& eventName() const override {
        static std::string n = "BlockBurnEvent"; return n;
    }
};
} // namespace
