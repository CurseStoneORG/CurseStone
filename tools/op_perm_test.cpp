// tools/op_perm_test.cpp
//
// Regression test for "un OP del ops.txt no puede ejecutar comandos de op".
//
// Exercises the REAL code path end to end, not a copy of it:
//   * SimpleCommandMap::dispatch() — the permission gate,
//   * Player::hasPermission()      — the op fallback for the "cursestone.*"
//                                    subtree (src/Player.h),
//   * Server::setOp()/isOp()       — the operator set behind ops.txt.
//
// It links against the project's own object files, so the sender really is
// cursestone::Player and the gate really is the one the server runs.
//
//   g++ -std=c++20 -Isrc -o /tmp/op_perm_test tools/op_perm_test.cpp \
//       $(ls /tmp/cs_build/obj/*.o | grep -v src_main.cpp.o) -lz -ldl -pthread
//   /tmp/op_perm_test
#include "Player.h"
#include "Server.h"
#include "command/Command.h"
#include "command/SimpleCommandMap.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const std::string& name) {
    printf("  [%s] %s\n", ok ? " OK " : "FAIL", name.c_str());
    if (!ok) ++failures;
}

// A stand-in for one of the op commands: same permission node the real
// defaults use, and it records whether execute() was reached.
class ProbeCommand : public cursestone::Command {
public:
    bool ran = false;
    ProbeCommand()
        : Command("probe", "test probe", "", "cursestone.command.probe") {}
    bool execute(cursestone::CommandSender* sender, const std::string&,
                 const std::vector<std::string>&) override {
        ran = true;
        sender->sendMessage("probe ran");
        return true;
    }
};

} // namespace

int main() {
    auto& srv = cursestone::Server::getInstance();

    cursestone::PlayerConfig cfg;
    cfg.username = "Dr1xy";
    cfg.runtime_eid = 1;
    auto player = std::make_shared<cursestone::Player>(&srv, cfg);

    cursestone::SimpleCommandMap map;
    map.registerCommand(std::make_unique<ProbeCommand>());
    auto* probe = static_cast<ProbeCommand*>(map.getCommand("probe"));

    // 1. Not an operator: the gate must refuse.
    probe->ran = false;
    map.dispatch(player.get(), "probe");
    check(!probe->ran, "non-op is refused by the permission gate");
    check(player->hasPermission("cursestone.command.probe") == false,
          "non-op hasPermission() is false for a cursestone.* node");

    // 2. Same player, now an operator (this is what an ops.txt entry does):
    //    the gate must let the command through.
    srv.setOp("Dr1xy", true);
    check(player->isOp(), "Player::isOp() sees the operator set");
    check(player->hasPermission("cursestone.command.probe"),
          "op hasPermission() is true for a cursestone.* node");
    probe->ran = false;
    map.dispatch(player.get(), "probe");
    check(probe->ran, "op reaches execute() through the gate");

    // 3. Name matching is case-insensitive, like the ops.txt loader.
    check(srv.isOp("dR1xY"), "Server::isOp() is case-insensitive");

    // 4. De-opping closes the gate again.
    srv.setOp("Dr1xy", false);
    probe->ran = false;
    map.dispatch(player.get(), "probe");
    check(!probe->ran, "de-opped player is refused again");

    printf("\n%s (%d failure(s))\n", failures ? "OP PERM TEST: FAIL" : "OP PERM TEST: OK",
           failures);
    return failures ? 1 : 0;
}
