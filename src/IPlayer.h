// IPlayer — interface implemented by Player and OfflinePlayer.
// Mirrors pocketmine/IPlayer.php.
#pragma once
#include <string>
namespace mcppeserver {
class IPlayer {
public:
    virtual ~IPlayer() = default;
    virtual std::string getName() const = 0;
    virtual bool isOnline() const = 0;
    virtual bool isOp() const = 0;
    virtual void setOp(bool v) = 0;
    virtual bool isBanned() const = 0;
    virtual void setBanned(bool v) = 0;
    virtual bool isWhitelisted() const = 0;
    virtual void setWhitelisted(bool v) = 0;
};
} // namespace
