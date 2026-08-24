// Command — base class for any registered command. Mirrors pocketmine
// /command/Command. Subclasses define execute() which is invoked by
// SimpleCommandMap when a player or the console types /<name>.
#pragma once
#include "CommandSender.h"
#include <string>
#include <vector>
namespace atomikpe {
class Command {
public:
    Command(std::string name, std::string description = "", std::string usage = "")
        : name_(std::move(name)), description_(std::move(description)), usage_(std::move(usage)) {}
    virtual ~Command() = default;
    virtual bool execute(CommandSender* sender, const std::string& label,
                         const std::vector<std::string>& args) = 0;
    const std::string& name() const { return name_; }
    const std::string& description() const { return description_; }
    const std::string& usage() const { return usage_; }
private:
    std::string name_, description_, usage_;
};
} // namespace
