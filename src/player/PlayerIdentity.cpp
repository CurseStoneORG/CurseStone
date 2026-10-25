/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
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
// PlayerIdentity — nametag + display name (the "ranks" surface).
//
// The reference server 0.15 split a player's names in two, and this file ports exactly
// that split:
//
//   - setNameTag(name)  (Genisys Player + Server::updatePlayerListData):
//     on protocol 84 the name rendered ABOVE THE HEAD and the one shown in
//     the SERVER LIST TAB both come from the PlayerListPacket ADD entry —
//     the tab is global (server-wide), and the overhead name of a player
//     body is synced from the same entry. So changing the nametag means
//     re-announcing the player: PlayerListPacket REMOVE(uuid) followed by
//     ADD(uuid, eid, newTag, skinId, skinData) to every spawned player,
//     self included. §-color codes ride along untouched, which is how
//     0.15 rank plugins (PurePerms-style) paint prefixes over heads.
//   - setDisplayName(name): chat-only alias, no packets; handleChat()
//     reads getDisplayName() so rank plugins can restyle the chat line.
//
// AddPlayerPacket sites keep using getNameTag() too (PlayerLogin.cpp), so
// clients that spawn a renamed player later still see the custom tag.
#include "Player.h"
#include "Server.h"
#include "network/protocol/PlayerListPacket.h"
#include "utils/MainLogger.hpp"
#include <array>
#include <memory>
#include <string>

namespace cursestone {

void Player::setNameTag(const std::string& tag) {
    nametag_ = tag;
    if (!spawned())
        return; // doFirstSpawn announces the tag on its own
    if (!srv_)
        return;

    // Server::updatePlayerListData() / removePlayerListData() (Genisys
    // Server): one REMOVE + one ADD broadcast over the global player
    // list. The 0.15.10 client refreshes both the tab entry and the
    // overhead name from this pair.
    std::array<uint8_t, 16> uuid{};
    parseUuidBytes(cfg_.uuid, uuid);

    auto rm = std::make_shared<network::protocol::PlayerListPacket>();
    rm->type = network::protocol::PlayerListPacket::TYPE_REMOVE;
    network::protocol::PlayerListPacket::Entry re;
    re.uuid = uuid;
    rm->entries.push_back(std::move(re));

    auto add = std::make_shared<network::protocol::PlayerListPacket>();
    add->type = network::protocol::PlayerListPacket::TYPE_ADD;
    network::protocol::PlayerListPacket::Entry e;
    e.uuid = uuid;
    e.eid = runtimeId();
    e.name = getNameTag();
    e.skinId = skinId();
    e.skinData = skinData();
    add->entries.push_back(std::move(e));

    for (auto& other : srv_->playersSnapshot()) {
        if (!other || !other->spawned())
            continue;
        other->sendPacket(rm);
        other->sendPacket(add);
    }
}

} // namespace cursestone
