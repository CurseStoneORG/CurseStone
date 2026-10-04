#include "SimpleCommandMap.h"
#include "utils/Utils.hpp"
namespace cursestone {
bool SimpleCommandMap::registerCommand(std::unique_ptr<Command> cmd) {
    if (!cmd) return false;
    std::string n = cmd->name();
    auto [it, ok] = commands_.try_emplace(std::move(n), std::move(cmd));
    return ok;
}
bool SimpleCommandMap::dispatch(CommandSender* sender, const std::string& cmd_line) {
    // Split on first space: name + args
    auto sp = cmd_line.find(' ');
    std::string name = (sp == std::string::npos) ? cmd_line : cmd_line.substr(0, sp);
    std::vector<std::string> args;
    if (sp != std::string::npos) {
        std::string rest = cmd_line.substr(sp + 1);
        args = utils::split(rest, ' ');
    }
    auto it = commands_.find(name);
    if (it == commands_.end()) {
        sender->sendMessage("Unknown command: " + name);
        return false;
    }
    // --- CONTROL DE PERMISOS ---
    //
    // Este era el agujero: dispatch() llamaba a execute() directo, sin
    // mirar nada. De los 15 comandos registrados, solo WorkbenchCommand
    // chequeaba isOp() en su propio cuerpo — los otros 14 (/stop, /op,
    // /deop, /give, /kick, /tp, /gamemode, /time, /weather, /save,
    // /say) los podía ejecutar CUALQUIER jugador sin ser op.
    //
    // Ahora el chequeo vive AQUÍ, en el punto único por el que pasan
    // todos los comandos, en vez de repartido en el cuerpo de cada uno
    // (donde es facil olvidar uno — que es exactamente lo que pasaba).
    // Un comando sin nodo declarado (permission_ vacio) sigue siendo
    // público: help, list, version.
    Command& cmd = *it->second;
    if (!cmd.isPublic() && !sender->hasPermission(cmd.permission())) {
        sender->sendMessage("You do not have permission to use this command.");
        return false;
    }
    try {
        return it->second->execute(sender, name, args);
    } catch (const std::exception& e) {
        sender->sendMessage("Error executing command: " + std::string(e.what()));
        return false;
    }
}
Command* SimpleCommandMap::getCommand(const std::string& name) const {
    auto it = commands_.find(name);
    return it == commands_.end() ? nullptr : it->second.get();
}
} // namespace
