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
// PlayerRespawnEvent — se dispara cuando un jugador va a reaparecer
// tras morir. `respawnX/Y/Z` arranca en el spawn point del nivel; un
// listener puede reescribirlo (p.ej. para implementar /sethome o un
// spawn por equipo).
//
// A diferencia de la mayoria de los eventos de este proyecto, este
// NO es Cancellable — mirrors PMMP PlayerRespawnEvent (extends
// PlayerEvent, no Cancellable). Cancelar un respawn dejaria al
// jugador permanentemente muerto sin ningun otro disparador para
// salir de ese estado, asi que la unica forma de influir en el
// resultado es cambiar DONDE reaparece, no si reaparece.
#pragma once
#include "event/Event.h"
#include <string>
namespace cursestone { class Player; }
namespace cursestone::event::player {
class PlayerRespawnEvent : public Event {
public:
    cursestone::Player* player;
    double respawnX = 0.0, respawnY = 0.0, respawnZ = 0.0;
    PlayerRespawnEvent(cursestone::Player* p, double x, double y, double z)
        : player(p), respawnX(x), respawnY(y), respawnZ(z) {}
    const std::string& eventName() const override {
        static std::string n = "PlayerRespawnEvent"; return n;
    }
};
} // namespace
