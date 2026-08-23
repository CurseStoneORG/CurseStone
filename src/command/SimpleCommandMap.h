#pragma once
#include "CommandMap.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
namespace cenisyspe {
class SimpleCommandMap : public CommandMap {
public:
    bool registerCommand(std::unique_ptr<Command> cmd) override;
    bool dispatch(CommandSender* sender, const std::string& cmd_line) override;
    Command* getCommand(const std::string& name) const override;
    size_t size() const { return commands_.size(); }
private:
    std::unordered_map<std::string, std::unique_ptr<Command>> commands_;
    std::vector<std::string> aliases_;
};
} // namespace
