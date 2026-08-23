#pragma once
#include "IPlayer.h"
#include <string>
namespace mcppeserver {
class Server;
class OfflinePlayer : public IPlayer {
public:
    OfflinePlayer(class Server* srv, std::string name) : srv_(srv), name_(std::move(name)) {}
    void sendMessage(const std::string& msg) override;
    std::string getName() const override { return name_; }
    bool hasPermission(const std::string&) const override;
    bool isOnline() const override { return false; }
    bool isOp() const override;
    void setOp(bool v) override;
    bool isBanned() const override;
    void setBanned(bool v) override;
    bool isWhitelisted() const override;
    void setWhitelisted(bool v) override;
private:
    Server* srv_;
    std::string name_;
};
} // namespace
