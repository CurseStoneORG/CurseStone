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
// PlayerJoinEvent — se dispara justo despues de que un jugador termina
// de spawnear en el mundo. `message` es el mensaje de broadcast
// ("Steve joined the game"); un listener puede reescribirlo, o
// vaciarlo para silenciar el aviso por completo.
//
// Cancelar el evento solo suprime el mensaje de broadcast — el
// jugador ya esta conectado y spawneado para cuando este evento se
// dispara, asi que cancelar no puede rechazar la conexion en si.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
namespace cursestone {
class Player;
}
namespace cursestone::event::player {
class PlayerJoinEvent : public Event, public Cancellable {
  public:
    cursestone::Player* player;
    std::string message;
    PlayerJoinEvent(cursestone::Player* p = nullptr, std::string msg = "")
        : player(p), message(std::move(msg)) {
    }
    const std::string& eventName() const override {
        static std::string n = "PlayerJoinEvent";
        return n;
    }
};
} // namespace
