// IPlayer — interface implemented by Player and OfflinePlayer.
// Mirrors pocketmine/IPlayer.php.
#pragma once
#include <string>
#include "command/CommandSender.h"
namespace cenisyspe {
class IPlayer : public CommandSender {
public:
    virtual bool isOnline() const = 0;
    virtual void setOp(bool v) = 0;
    virtual bool isBanned() const = 0;
    virtual void setBanned(bool v) = 0;
    virtual bool isWhitelisted() const = 0;
    virtual void setWhitelisted(bool v) = 0;
};
} // namespace
