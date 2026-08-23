#pragma once
#include "CommandSender.h"
#include <string>
namespace cenisyspe {
class ConsoleCommandSender : public CommandSender {
public:
    void sendMessage(const std::string& msg) override;
    std::string getName() const override { return "CONSOLE"; }
    bool hasPermission(const std::string&) const override { return true; }
    bool isOp() const override { return true; }
};
} // namespace
