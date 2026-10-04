// tools/command_node_check.cpp
//
// Verifica el nodo de permiso REAL declarado por cada comando
// registrado en src/command/Defaults.cpp.
//
// tools/command_permission_check.cpp ejercita el gate de
// SimpleCommandMap::dispatch con comandos de prueba. Este complementa
// esa cobertura con el otro extremo: que los 15 comandos que el servidor
// registra de verdad esten ETIQUETADOS, para que un op nuevo no
// nazca como comando publico por olvido.
//
// Que un comando lleve nodo NO es opcional segun el pedido: "haz que los
// jugadores que no sean ops no puedan ejecutar comandos que no tienen
// permisos". help/list/version siguen publicas a proposito.
//
// Construye los comandos de verdad (solo el constructor, que es lo que
// fija el nodo) y los lee con Command::permission(). Los cuerpos de
// execute() nunca se invocan; los stubs de enlace abortan si lo hicieran.
//
//   g++ -std=c++20 -Wall -Wextra -O1 -Isrc -I/tmp/zstub
//       tools/command_node_check.cpp tools/command_nodes_stubs.cpp
//       src/command/Defaults.cpp src/command/SimpleCommandMap.cpp
//       src/command/defaults/*.cpp
//       -o /tmp/tools/command_node_check
#include "command/Defaults.h"
#include "command/SimpleCommandMap.h"
#include "command/Command.h"

#include <cstdio>
#include <string>

using namespace cursestone;

static int failures = 0;
static void check(bool ok, const std::string& what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

struct Expect {
    const char* name;
    const char* node;      // "" = debe seguir siendo publica
    bool present;
};

int main() {
    printf("COMMAND NODE CHECK (nodos reales de Defaults.cpp)\n");

    SimpleCommandMap map;
    registerDefaultCommands(map);

    // Comandos que DEBEN estar restringidos a ops, con su nodo exacto.
    const Expect restricted[] = {
        { "stop",     "cursestone.command.stop",     true },
        { "kick",     "cursestone.command.kick",     true },
        { "give",     "cursestone.command.give",     true },
        { "op",       "cursestone.command.op",       true },
        { "deop",     "cursestone.command.deop",     true },
        { "save",     "cursestone.command.save",     true },
        { "say",      "cursestone.command.say",      true },
        { "time",     "cursestone.command.time",     true },
        { "weather",  "cursestone.command.weather",  true },
        { "workbench","cursestone.command.workbench",true },
        // Alias: deben compartir nodo con su comando base, o el alias
        // seria una puerta trasera.
        { "gamemode", "cursestone.command.gamemode", true },
        { "gm",       "cursestone.command.gamemode", true },
        { "teleport", "cursestone.command.teleport", true },
        { "tp",       "cursestone.command.teleport", true },
    };
    // Comandos que deben seguir disponibles para cualquiera.
    const Expect publicCmds[] = {
        { "help",    "", true },
        { "?",       "", true },
        { "version", "", true },
        { "list",    "", true },
    };

    printf("\n-- restringidos (solo ops) --\n");
    for (const Expect& e : restricted) {
        Command* c = map.getCommand(e.name);
        if (!c) {
            check(false, std::string(e.name) + " esta registrado");
            continue;
        }
        std::string got = c->permission();
        printf("    %-9s -> %s\n", e.name, got.empty() ? "(SIN NODO!)" : got.c_str());
        check(got == e.node, std::string(e.name) + " declara el nodo " + e.node);
    }

    printf("\n-- publicos (cualquiera) --\n");
    for (const Expect& e : publicCmds) {
        Command* c = map.getCommand(e.name);
        if (!c) {
            check(false, std::string(e.name) + " esta registrado");
            continue;
        }
        std::string got = c->permission();
        printf("    %-9s -> %s\n", e.name, got.empty() ? "(publico)" : got.c_str());
        check(got.empty(), std::string(e.name) + " sigue siendo publico");
    }

    // Ningun comando con nodo puede quedar huerfano fuera del arbol
    // cursestone.*, porque Player::hasPermission() solo concede ese
    // sub-arbol a los ops: un nodo mal escrito dejaria el comando
    // bloqueado para TODO el mundo, incluidos los ops.
    printf("\n-- todos los nodos viven bajo cursestone.* --\n");
    static const std::string kRoot = "cursestone.";   // 11 chars
    for (const Expect& e : restricted) {
        Command* c = map.getCommand(e.name);
        if (!c) continue;
        std::string n = c->permission();
        bool okRoot = n.size() > kRoot.size() && n.compare(0, kRoot.size(), kRoot) == 0;
        check(okRoot, std::string(e.name) + " usa un nodo bajo cursestone.*");
    }

    printf("\nCOMMAND NODE CHECK: %s (%d failure(s))\n",
           failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}