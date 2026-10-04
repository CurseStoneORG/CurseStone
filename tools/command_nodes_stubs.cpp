// tools/command_nodes_stubs.cpp
//
// Stubs de enlace para tools/command_node_check.cpp.
//
// El probe de permisos de comandos SOLO necesita CONSTRUIR los
// comandos de src/command/defaults/ para leer su nodo de permiso
// (Command::permission()). Los cuerpos de execute() — que son los que
// tocan Server, Player, Level y WorkbenchManager — nunca se ejecutan,
// pero el linker los exige porque la vtable los referencia.
//
// Estos stubs los definen como abort(): si alguno llegara a ejecutarse
// de verdad, el probe aborta en vez de dar un resultado silenciosamente
// falso. Es una guarda, no un silencio.
#include "Player.h"
#include "Server.h"
#include "command/CommandReader.h"
#include "permission/BanList.h"
#include "workbench/WorkbenchManager.h"
#include "level/Level.h"

#include <cstdlib>
#include <cstdio>

namespace {
[[noreturn]] void stub(const char* what) {
    fprintf(stderr, "STUB CALLED: %s — the probe must not execute commands\n", what);
    abort();
}
} // namespace

namespace cursestone {

// --- Player: solo la clave virtual, para que se emita el typeinfo ---
Player::~Player() = default;
void Player::kick(const std::string&) { stub("Player::kick"); }
void Player::sendPacket(std::shared_ptr<network::protocol::DataPacket>, bool) { stub("Player::sendPacket"); }
void Player::setGamemode(int) { stub("Player::setGamemode"); }
void Player::teleport(double, double, double) { stub("Player::teleport"); }

// Accesores que la vtable de Player necesita. El probe nunca los
// invoca: no crea jugadores, solo lee nodos de permisos de comandos.
bool Player::isOp() const { return false; }
void Player::setOp(bool) { stub("Player::setOp"); }
bool Player::isBanned() const { return false; }
void Player::setBanned(bool) { stub("Player::setBanned"); }
bool Player::isWhitelisted() const { return true; }
void Player::setWhitelisted(bool) { stub("Player::setWhitelisted"); }
void Player::sendMessage(const std::string&) { stub("Player::sendMessage"); }
void Player::onDeath() { stub("Player::onDeath"); }
void Player::onHealthChanged() { stub("Player::onHealthChanged"); }

// --- Server ---
Server::Server() = default;
Server::~Server() = default;
void Server::broadcastMessage(const std::string&) { stub("Server::broadcastMessage"); }
Player* Server::getPlayer(const std::string&) const { stub("Server::getPlayer"); return nullptr; }
std::vector<std::shared_ptr<Player>> Server::playersSnapshot() const { stub("Server::playersSnapshot"); return {}; }
void Server::setOp(const std::string&, bool) { stub("Server::setOp"); }
void Server::shutdown() { stub("Server::shutdown"); }

// --- WorkbenchManager ---
WorkbenchManager::~WorkbenchManager() = default;
void WorkbenchManager::start()  { stub("WorkbenchManager::start"); }
void WorkbenchManager::stop()   { stub("WorkbenchManager::stop"); }
void WorkbenchManager::status() { stub("WorkbenchManager::status"); }
void WorkbenchManager::report() { stub("WorkbenchManager::report"); }

// --- Level ---
int  level::Level::save() { stub("Level::save"); return 0; }
void level::Level::setWeather(int, int32_t) { stub("Level::setWeather"); }

} // namespace cursestone