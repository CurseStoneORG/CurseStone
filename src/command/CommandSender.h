// CommandSender — anything that can issue commands (console,
// players, RCON clients). Mirrors pocketmine/command/CommandSender.
#pragma once
#include <string>
#include <memory>
namespace mcppeserver {
class CommandSender {
public:
    virtual ~CommandSender() = default;
    // Where to send messages back to.
    virtual void sendMessage(const std::string& msg) = 0;
    // Identity: "CONSOLE" or player username.
    virtual std::string getName() const = 0;
    // Whether the sender has the given permission node.
    virtual bool hasPermission(const std::string& perm) const = 0;
    // Whether the sender is the server console.
    virtual bool isOp() const = 0;
};
} // namespace
