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
// RedstoneManager.h — redstone signal propagation engine.
//
// Design (hardened after the BlockFire v6 88s freeze from unbounded
// cascades): wire propagation is capped at 15 power steps (a hard
// mathematical bound, not a heuristic); every update is a synchronous,
// bounded computation finishing within the tick that requested it;
// notifyNeighbors() is the only entry point external code calls.
//
// FIX: state changes are broadcast with UpdateBlockPacket (+ sound /
// particle when applicable) the same way Player.cpp does for
// break/place, so propagation is visible live instead of only after
// reconnecting.
#pragma once
#include <cstdint>

namespace cursestone::world {
class World;
}
namespace cursestone {
class Server;
class Player;
}

namespace cursestone::redstone {

// Se llama tras romper o colocar CUALQUIER bloque en (x,y,z). Revisa los
// 6 vecinos inmediatos y, si alguno es un componente de redstone
// wire, torch, lever, button, pressure plate, redstone block — fires
// the matching update and broadcasts every visible change to players
// viewing that chunk.
//
// This is the main entry point for the rest of the project; everything
// else in this module is internal.
void notifyNeighbors(cursestone::world::World* world, cursestone::Server* server, int32_t x,
                     int32_t y, int32_t z);

// Recomputes the power (0-15) a redstone component should holdwire en (x,y,z) dado su entorno actual, y actualiza su `data` en el
// and writes it to the world if changed, cascading to neighbor wires
// when the power change affects them (max 15 steps).
// Transmite cada wire que cambie a los jugadores cercanos.
void updateWireAt(cursestone::world::World* world, cursestone::Server* server, int32_t x, int32_t y,
                  int32_t z);

// Recomputes whether a redstone lamp at (x,y,z) should be lit
// (LIT_REDSTONE_LAMP) or dark (REDSTONE_LAMP) and broadcasts the change.
// A lamp only consumes signal, so unlike the wire there is
// riesgo de cascada al llamar esto.
void updateLampAt(cursestone::world::World* world, cursestone::Server* server, int32_t x, int32_t y,
                  int32_t z);

// Returns the signal strength (0-15) the block at (x,y,z) emits
// hacia afuera — considera antorchas encendidas, bloques de redstone,
// wire ya calculado, y palancas/botones activados. Devuelve 0 si el
// block is not a source or is off. Read-only, so no Server* needed.
int getEmittedPower(cursestone::world::World* world, int32_t x, int32_t y, int32_t z);

// True when the block at (x,y,z) currently receives redstone power
// (> 0) from any neighbor or from itself (wires). Doors, pistons and
// lamps query this
// decidir si deben activarse.
bool isPowered(cursestone::world::World* world, int32_t x, int32_t y, int32_t z);

// Intenta interactuar con un componente de redstone accionable por el
// player (lever, button, wooden door — iron doors never react to
// clicks, only to redstone, like vanilla) at (x,y,z). If the block is
// one of those types,
// alterna/activa su estado, actualiza el mundo, transmite el cambio
// and returns true. Levers and buttons additionally notify neighbors
// (pueden alimentar un circuito); la puerta NO lo hace al recibir
// manual click — see the .cpp note for why. Non-interactive blocks do
// nothing and return false, so the caller (Player) knows to continue
// with its own item placement logic.
//
// `player` identifies who toggled the component (for DoorToggleEvent;
// nullable). Doors also emit the vanilla open/close sound
// (LevelEventPacket evid=1003 — fix de "las puertas no emiten sonido")
// y dispara DoorToggleEvent, cancelable.
bool tryInteract(cursestone::world::World* world, cursestone::Server* server, int32_t x, int32_t y,
                 int32_t z, cursestone::Player* player = nullptr);

// Recalcula si una antorcha de redstone en (x,y,z) debe estar
// encendida (id REDSTONE_TORCH_ON) o apagada (id REDSTONE_TORCH_OFF).
// Vanilla behavior (the torch is a logical INVERTER, not a source like
// simple foco): se apaga si el bloque que la sostiene (el vecino hacia
// it points at, usually below) receives signal, and lights up when that
// support has none — the NOT gate of real circuits. Only the standing
// antorchas orientadas hacia abajo (colocadas sobre un bloque) — es el
// caso que cubre notifyNeighbors hoy; antorchas en pared quedan como
// documented future work (see the .cpp comment).
void updateRedstoneTorchAt(cursestone::world::World* world, cursestone::Server* server, int32_t x,
                           int32_t y, int32_t z);

// Recalcula si una puerta (madera o hierro) debe estar abierta o
// on whether its LOWER half receives redstone signal. Doors span two
// vertical blocks; only the lower half stores the hinge+open bit (0x04)
// and only it is queried — like vanilla. Given the upper half, the
// function resolves the lower one (y-1) automatically
// que notifyNeighbors pueda llamarla con cualquiera de las dos mitades
// without needing to know which was passed. Syncs bit 0x04 across both
// halves
// y transmite el cambio de las que hayan cambiado.
void updateDoorAt(cursestone::world::World* world, cursestone::Server* server, int32_t x, int32_t y,
                  int32_t z);

// Recomputes whether a piston (regular or sticky) at (x,y,z) should be
// extended. Extending: if the block in front (facing from bits 0-2 of
// `data`) is air or replaceable, places a PISTON_HEAD there and pushes
// that content out
// (lo reemplaza por aire, sin generar drop — igual que vanilla, que no
// dropea el bloque empujado, lo reubica). Empuje limitado a UN bloque
// and only when it is not adjacent to another solid: long push chains
// and the vanilla 12-block limit are documented future work
// futura documentada (ver comentario en el .cpp) — cubre el circuito
// for the typical case (pushing a single block) without risking
// unbounded chains. Retracting removes the PISTON_HEAD; a sticky piston
// pulling the block back is also documented future work.
void updatePistonAt(cursestone::world::World* world, cursestone::Server* server, int32_t x,
                    int32_t y, int32_t z);

} // namespace cursestone::redstone
