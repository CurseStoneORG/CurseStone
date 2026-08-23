#pragma once
#include "Command.h"
#include <string>
#include <vector>
namespace cenisyspe {
class CommandMap {
public:
    virtual ~CommandMap() = default;
    virtual bool registerCommand(std::unique_ptr<Command> cmd) = 0;
    virtual bool dispatch(CommandSender* sender, const std::string& cmd_line) = 0;
    virtual Command* getCommand(const std::string& name) const = 0;
};
} // namespace
