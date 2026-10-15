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
// PlayerMoveEvent — se dispara cuando el cliente reporta una nueva
// posicion valida (handleMovePlayer ya paso las validaciones basicas
// de anti-cheat / NaN / rango de Y antes de disparar esto).
//
// Alta frecuencia: se dispara por CADA MovePlayerPacket aceptado, es
// decir potencialmente varias veces por tick por jugador. Un listener
// puede leer from/to para logica de zonas, o cancelar para revertir
// el movimiento (ver el comentario en Player::handleMovePlayer sobre
// como se reenvia la posicion original al cliente si se cancela).
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
namespace cursestone {
class Player;
}
namespace cursestone::event::player {
class PlayerMoveEvent : public Event, public Cancellable {
  public:
    cursestone::Player* player;
    double fromX = 0.0, fromY = 0.0, fromZ = 0.0;
    double toX = 0.0, toY = 0.0, toZ = 0.0;
    PlayerMoveEvent(cursestone::Player* p, double fx, double fy, double fz, double tx, double ty,
                    double tz)
        : player(p), fromX(fx), fromY(fy), fromZ(fz), toX(tx), toY(ty), toZ(tz) {
    }
    const std::string& eventName() const override {
        static std::string n = "PlayerMoveEvent";
        return n;
    }
};
} // namespace
