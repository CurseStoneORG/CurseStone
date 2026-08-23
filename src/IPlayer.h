// IPlayer — interface implemented by Player and OfflinePlayer.
// Mirrors pocketmine/IPlayer.php.
// Inherits from CommandSender so Player can be dispatched commands.
#pragma once
#include "command/CommandSender.h"
#include <string>
namespace mcppeserver {
class IPlayer : public CommandSender {
public:
    virtual ~IPlayer() = default;
    // CommandSender overrides (getName, isOp already declared there)
    virtual bool isOnline() const = 0;
    virtual void setOp(bool v) = 0;
    virtual bool isBanned() const = 0;
    virtual void setBanned(bool v) = 0;
    virtual bool isWhitelisted() const = 0;
    virtual void setWhitelisted(bool v) = 0;
};
} // namespace
