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
// PlayerChatEvent — se dispara cuando un jugador envia un mensaje de
// chat (no un comando). `message` es el texto a difundir; un listener
// puede reescribirlo (por ejemplo para anteponer un prefijo de rango)
// antes de que se difunda a los demas jugadores. Cancelar el evento
// evita el broadcast por completo.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
namespace cursestone {
class Player;
}
namespace cursestone::event::player {
class PlayerChatEvent : public Event, public Cancellable {
  public:
    cursestone::Player* player;
    std::string message;
    PlayerChatEvent(cursestone::Player* p = nullptr, std::string msg = "")
        : player(p), message(std::move(msg)) {
    }
    const std::string& eventName() const override {
        static std::string n = "PlayerChatEvent";
        return n;
    }
};
} // namespace
