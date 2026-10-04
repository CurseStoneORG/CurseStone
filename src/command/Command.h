// Command — base class for any registered command. Mirrors pocketmine
// /command/Command. Subclasses define execute() which is invoked by
// SimpleCommandMap when a player or the console types /<name>.
#pragma once
#include "CommandSender.h"
#include <string>
#include <vector>
namespace cursestone {
class Command {
public:
    // `permission` es el nodo de permiso que el emisor debe tener para
    // poder ejecutar este comando. VACIO = cualquiera puede ejecutarlo
    // (help, list, version...). Un nodo como "cursestone.command.stop"
    // loMarco SimpleCommandMap::dispatch() antes de invocar execute(),
    // de modo que un comando sin permiso NUNCA llega al execute() — ni
    // siquiera si un comando forgets de revisar isOp() en su cuerpo.
    Command(std::string name, std::string description = "", std::string usage = "",
            std::string permission = "")
        : name_(std::move(name)), description_(std::move(description)), usage_(std::move(usage)),
          permission_(std::move(permission)) {}
    virtual ~Command() = default;
    virtual bool execute(CommandSender* sender, const std::string& label,
                         const std::vector<std::string>& args) = 0;
    const std::string& name() const { return name_; }
    const std::string& description() const { return description_; }
    const std::string& usage() const { return usage_; }
    // Nodo requerido, o "" si es público.
    const std::string& permission() const { return permission_; }
    bool isPublic() const { return permission_.empty(); }
private:
    std::string name_, description_, usage_, permission_;
};
} // namespace
