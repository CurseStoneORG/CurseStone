// tools/command_permission_check.cpp
//
// El segundo bug reportado: "haz que los jugadores que no sean ops no
// puedan ejecutar comandos que no tienen permisos".
//
// Estado previo (verificado sobre el codigo de este repo): de los 15
// comandos registrados en src/command/Defaults.cpp, SOLO
// WorkbenchCommand chequeaba isOp() dentro de su propio execute().
// Los otros 14 — /stop, /op, /deop, /give, /kick, /tp, /gamemode,
// /time, /weather, /save, /say — no tenian ninguna comprobacion, y
// SimpleCommandMap::dispatch() llamaba a execute() sin mirar nada. Es
// decir: cualquier jugador conectado podia apagar el servidor, darse op
// a si mismo o teletransportarse.
//
// El arreglo mueve el chequeo al punto unico por el que pasan todos los
// comandos (SimpleCommandMap::dispatch) y lo declara por comando con un
// nodo "cursestone.command.<nombre>". Este arnés ejercita ESE codigo
// real: registra comandos de prueba contra la SimpleCommandMap de
// producción y despacha como un emisor sin permisos y como uno con
// permisos.
//
//   g++ -std=c++20 -Wall -Wextra -O1 -Isrc -I/tmp/zstub
//       tools/command_permission_check.cpp
//       src/command/SimpleCommandMap.cpp
//       -o /tmp/tools/command_permission_check
#include "command/SimpleCommandMap.h"
#include "command/Command.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace cursestone;

static int failures = 0;
static void check(bool ok, const std::string& what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// Emisor de pruebas. `ops` decide si tiene el sub-arbol cursestone.*,
// replicando exactamente la regla de Player::hasPermission() para ops.
struct FakeSender : CommandSender {
    std::string name;
    bool ops;
    mutable std::vector<std::string> out;
    FakeSender(std::string n, bool o) : name(std::move(n)), ops(o) {}
    void sendMessage(const std::string& m) override { out.push_back(m); }
    std::string getName() const override { return name; }
    bool isOp() const override { return ops; }
    bool hasPermission(const std::string& perm) const override {
        if (perm.empty()) return true;
        static const std::string kRoot = "cursestone.";
        if (!ops) return false;
        return perm.size() > kRoot.size() && perm.compare(0, kRoot.size(), kRoot) == 0;
    }
};

// Comando de prueba: execute() devuelve true si llego a ejecutarse.
struct ProbeCommand : Command {
    bool* executed;
    std::string node;
    ProbeCommand(std::string name, std::string node, bool* executed)
        : Command(name, "probe", "", node), executed(executed) {}
    bool execute(CommandSender*, const std::string&, const std::vector<std::string>&) override {
        *executed = true;
        return true;
    }
};

int main() {
    printf("COMMAND PERMISSION CHECK\n");

    // ---- 1. El gate bloquea a un jugador sin permisos -------------
    {
        SimpleCommandMap map;
        bool ran = false;
        map.registerCommand(std::make_unique<ProbeCommand>("stop", "cursestone.command.stop", &ran));

        FakeSender guest("guest", /*ops=*/false);
        bool ok = map.dispatch(&guest, "stop");
        check(!ran, "a non-op CANNOT execute /stop (execute() never reached)");
        check(!ok, "dispatch() reports failure for a non-op");
        check(guest.out.size() == 1 &&
              guest.out[0].find("permission") != std::string::npos,
              "a non-op gets a 'no permission' message");
    }

    // ---- 2. ...pero un op si puede --------------------------------
    {
        SimpleCommandMap map;
        bool ran = false;
        map.registerCommand(std::make_unique<ProbeCommand>("stop", "cursestone.command.stop", &ran));

        FakeSender admin("admin", /*ops=*/true);
        bool ok = map.dispatch(&admin, "stop");
        check(ran, "an op CAN execute /stop");
        check(ok, "dispatch() reports success for an op");
    }

    // ---- 3. Los permisos son POR COMANDO, no globales --------------
    {
        SimpleCommandMap map;
        bool ranStop = false, ranList = false;
        map.registerCommand(std::make_unique<ProbeCommand>("stop", "cursestone.command.stop", &ranStop));
        map.registerCommand(std::make_unique<ProbeCommand>("help", "", &ranList));  // público

        FakeSender guest("guest", false);
        map.dispatch(&guest, "stop");
        check(!ranStop, "/stop blocked for non-op");
        map.dispatch(&guest, "help");
        check(ranList, "/help (no permission node) stays public for everyone");
    }

    // ---- 4. Un op no puede saltarse el gate con otro nodo ----------
    {
        SimpleCommandMap map;
        bool ran = false;
        map.registerCommand(std::make_unique<ProbeCommand>("gamemode", "cursestone.command.gamemode", &ran));
        FakeSender guest("guest", false);
        map.dispatch(&guest, "gamemode 1 creative");
        check(!ran, "/gamemode blocked for non-op (with args too)");
        // Un nodo fuera del arbol cursestone.* tampoco se concede por ser op.
        map.registerCommand(std::make_unique<ProbeCommand>("weird", "otro.nodo", &ran));
        FakeSender admin("admin", true);
        map.dispatch(&admin, "weird");
        check(!ran, "an op does NOT automatically hold foreign permission roots");
    }

    // ---- 5. Comando inexistente sigue funcionando ------------------
    {
        SimpleCommandMap map;
        FakeSender guest("guest", false);
        bool ok = map.dispatch(&guest, "nope");
        check(!ok, "unknown command still returns false");
        check(guest.out.size() == 1 &&
              guest.out[0].find("Unknown") != std::string::npos,
              "unknown command reports 'Unknown command'");
    }

    // ---- 6. GameMode/Teleport comparten nodo entre alias ----------
    {
        // /gm debe comportarse igual que /gamemode; ambos se construyen
        // con el mismo nodo, asi que el alias no puede esquivarlo.
        SimpleCommandMap map;
        bool ranA = false, ranB = false;
        map.registerCommand(std::make_unique<ProbeCommand>("gamemode", "cursestone.command.gamemode", &ranA));
        map.registerCommand(std::make_unique<ProbeCommand>("gm", "cursestone.command.gamemode", &ranB));
        FakeSender guest("guest", false);
        map.dispatch(&guest, "gm");
        map.dispatch(&guest, "gamemode");
        check(!ranA && !ranB, "both /gamemode and its /gm alias are blocked for non-ops");
    }

    printf("\nCOMMAND PERMISSION CHECK: %s (%d failure(s))\n",
           failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}